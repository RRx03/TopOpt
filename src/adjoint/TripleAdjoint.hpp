#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Sparse>

#include "core/Grid3D.hpp"
#include "fem/H8Element.hpp"

namespace topopt {

// Discrete TRIPLE-coupled adjoint (Phase 5 gate), CPU double precision, fully
// self-contained (Eigen direct solves, unit-cube H8 cells, h = 1). Everything —
// the three coupled operators A, K_t, K_e and the forward objective — is
// assembled inside this class so the discrete adjoint differentiates EXACTLY the
// operators used by the finite-difference oracle.
//
// One-way primal cascade in the design field gamma (per element, gamma=1 fluid,
// gamma=0 solid):
//   1. Stokes-Brinkman   A(gamma) w = f_s ,   w = (u,p), 4 DOF/node.
//        A = viscous(mu*L) + B/Bt (pressure) + PSPG(-tau*L) + alpha(gamma)*M_vel
//        alpha(gamma) = amax + (amin-amax) g (1+q)/(g+q)   (Borrvall-Petersson)
//   2. CHT (Galerkin, NO SUPG)   K_t(gamma,u) T = f_t
//        K_t = k(gamma) L0 (diffusion, symmetric) + C_a(u) (advection, N_a u.grad N_b)
//        k(gamma) = ks + (kf-ks) gamma
//   3. Thermo-elastic    K_e(gamma) U = f_mech + F_th(gamma,T)
//        E(gamma) = Emin + (E0-Emin)(1-gamma)^p   (solid rigid at gamma=0)
//        F_th = sum_e E_e alpha_th Cth (T_e - Tref)
// Objective J = L^T U with L = Fmech (fixed linear functional).
//
// Adjoint (inverse cascade, Lagrangian L = J + le^T Re + lt^T Rt + ls^T Rs):
//   1. K_e^T le = -L                          (K_e SPD)
//   2. K_t^T lt = G^T le,  G = dF_th/dT        (advection transposes)
//   3. A^T  ls = -(dRt/du)^T lt,               (dRt,a/du_{b,c} = int N_a N_b dT/dx_c)
//                pressure DOFs of the RHS are zero.
//   grad_i = le^T[(dKe/dg_i)U - dF_th/dg_i] + lt^T(dKt/dg_i)T + ls^T(dA/dg_i)w
class TripleAdjoint {
public:
    using Vec = Eigen::VectorXd;
    using SpMat = Eigen::SparseMatrix<double>;

    struct Params {
        // Stokes / Brinkman
        double mu = 1.0;
        double alphaStab = 1.0 / 12.0;  // PSPG coefficient (as in StokesSolver)
        double alphaMax = 0.0;          // Brinkman inverse-permeability bounds
        double alphaMin = 0.0;
        double qBrink = 0.1;            // Borrvall-Petersson convexity knob
        // CHT conductivity (two-phase)
        double ks = 1.0;                // solid conductivity (gamma=0)
        double kf = 1.0;                // fluid  conductivity (gamma=1)
        // Thermo-elastic
        double E0 = 1.0;
        double Emin = 1e-4;
        double p = 3.0;
        double alphaTh = 1e-3;          // thermal-expansion coupling coefficient
        double Tref = 0.0;
        double nu = 0.3;
    };

    struct Solution {
        Vec w;   // Stokes solution (4*nNodes)
        Vec T;   // nodal temperature (nNodes)
        Vec U;   // nodal displacement (nDof)
        double J = 0.0;
        Vec grad;         // dJ/dgamma (nElems)
        Vec termStokes;   // ls^T (dA/dg) w
        Vec termThermal;  // lt^T (dKt/dg) T
        Vec termElastic;  // le^T[(dKe/dg)U - dF_th/dg]
    };

    TripleAdjoint(const Grid3D& grid, const Params& prm,
                  const std::vector<int>& stokesFixedDofs,
                  const std::array<double, 3>& bodyForce,
                  const std::vector<std::uint8_t>& thermalDirMask,
                  const Vec& thermalDirVal, const Vec& Q,
                  const std::vector<int>& elasticFixedDofs, const Vec& Fmech);

    // --- von Mises p-norm objective through the SAME triple cascade ----------
    // (GATE A2). Solidity s_e = 1 - gamma_e (v3 convention: gamma=1 fluid).
    // Solid centroid stress sigma0_e = sqrt((S0 u_e)^T V (S0 u_e)) with S0 the
    // unit-modulus (E = 1) centroid stress matrix — sigma0 does NOT carry E(g):
    // all explicit gamma dependence sits in the qp relaxation
    //   sigma_e = s_e^q * sigma0_e ,  J_sigma = (sum_e sigma_e^P)^(1/P).
    // Gradient = explicit relaxation term  dJ/dsigma_e * d(s^q)/dg * sigma0_e
    // (d s/dg = -1) + the SAME inverse cascade as solve(), only the elastic
    // adjoint seed changes: K_e le = -dJ_sigma/dU instead of -Fmech.
    struct StressParams {
        double q = 0.5;  // qp-relaxation exponent (< SIMP p)
        double P = 8.0;  // p-norm aggregation exponent
    };

    struct StressSolution {
        Vec w;   // Stokes solution (4*nNodes)
        Vec T;   // nodal temperature (nNodes)
        Vec U;   // nodal displacement (nDof)
        double J = 0.0;   // sigma p-norm
        Vec vm0;          // solid (unrelaxed) von Mises per element
        Vec grad;         // dJ_sigma/dgamma (nElems)
        Vec termExplicit; // relaxation: dJ/dsig * d(s^q)/dg * sigma0
        Vec termStokes;   // ls^T (dA/dg) w
        Vec termThermal;  // lt^T (dKt/dg) T
        Vec termElastic;  // le^T[(dKe/dg)U - dF_th/dg]
    };

    // Full forward cascade -> J = Fmech^T U (used by the FD oracle).
    double objective(const Vec& gamma) const;

    // Forward + the three discrete adjoint solves + gradient.
    Solution solve(const Vec& gamma) const;

    // Full forward cascade -> J_sigma (von Mises p-norm FD oracle).
    double stressObjective(const Vec& gamma, const StressParams& sp) const;

    // Forward + inverse cascade seeded by -dJ_sigma/dU + gradient.
    StressSolution solveStress(const Vec& gamma, const StressParams& sp) const;

    // Peak nodal speed of the current Stokes solution (diagnostic / Peclet).
    double maxSpeed(const Vec& w) const;

    static int sdof(int node, int c) { return 4 * node + c; }

private:
    // Brinkman interpolation and its derivative.
    double alpha(double g) const;
    double dAlpha(double g) const;
    double youngE(double g) const;
    double dYoungE(double g) const;

    // --- assembly (full, unreduced) -----------------------------------------
    void buildStokes(const Vec& gamma, SpMat& Afree, Vec& ffree) const;
    SpMat buildThermalFull(const Vec& gamma, const Vec& vel3) const;
    SpMat buildElasticFull(const Vec& gamma) const;
    Vec thermalSource() const;                       // int N Q  (full nNodes)
    Vec thermalLoad(const Vec& gamma, const Vec& T) const;   // F_th (full nDof)

    // Adjoint right-hand sides.
    Vec thermalAdjointRhs(const Vec& gamma, const Vec& lamE) const;  // G^T le (full)
    Vec stokesAdjointRhs(const Vec& T, const Vec& lamT) const;       // (full 4N)

    void forward(const Vec& gamma, Vec& w, Vec& T, Vec& U) const;
    Vec velocity3(const Vec& w) const;               // extract nodal u (3N)

    const Grid3D& grid_;
    Params prm_;
    std::array<double, 3> bodyForce_;
    Vec dirValT_;
    std::vector<std::uint8_t> dirMaskT_;
    Vec Q_;
    Vec Fmech_;

    // Element operators on the unit cube (h = 1).
    H8Element::Mat24 ke0_;      // elastic stiffness (E = 1)
    H8Element::Mat24x8 cth_;    // thermal coupling
    H8Element::Mat8 l0_;        // scalar diffusion (= viscous Laplacian block)
    H8Element::Mat8 mvel_;      // velocity mass (Brinkman)
    Eigen::Matrix<double, 32, 32> stokesKe_;  // constant saddle-point element mat

    // Reduced-DOF maps (global -> reduced, -1 if constrained).
    std::vector<int> sMap_;  int nFreeS_ = 0;   // Stokes (4N)
    std::vector<int> tMap_;  int nFreeT_ = 0;   // thermal (N)
    std::vector<int> eMap_;  int nFreeE_ = 0;   // elastic (3N)
};

} // namespace topopt
