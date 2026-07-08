#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Sparse>

#include "core/Grid3D.hpp"
#include "fem/H8Element.hpp"

namespace topopt {

// Wall peak-temperature objective + its discrete adjoint gradient (Phase 5R),
// CPU double precision, self-contained. Reuses EXACTLY the Stokes-Brinkman -> CHT
// forward and the transposed adjoint solves (K_t^T, A^T) and the thermal->Stokes
// coupling of TripleAdjoint. NO elastic block here.
//
// One-way primal cascade in the design field gamma (gamma=1 fluid, gamma=0 solid):
//   1. Stokes-Brinkman   A(gamma) w = f_s ,   w = (u,p), 4 DOF/node.
//        A = viscous(mu*L) + B/Bt (pressure) + PSPG(-tau*L) + alpha(gamma)*M_vel
//        alpha(gamma) = amax + (amin-amax) g (1+q)/(g+q)   (Borrvall-Petersson)
//   2. CHT (Galerkin, NO SUPG)   K_t(gamma,u) T = f_t
//        K_t = k(gamma) L0 (diffusion) + C_a(u) (advection, N_a u.grad N_b)
//        k(gamma) = ks + (kf-ks) gamma
//
// Objective (p-norm of temperature over the SOLID wall):
//   J_T = ( sum_e s_e T_e^P )^{1/P},   T_e = (1/8) sum_{a in e} T_a,
//   s_e = 1 - gamma_e  (solidity weight: we measure T INSIDE the solid wall),
//   P = 8.  Depends on gamma (via k(gamma) AND via s_e=1-gamma), u (advection), T.
//
// Adjoint (inverse cascade, Lagrangian L = J_T + lt^T Rt + ls^T Rs):
//   1. K_t^T lt = -dJ_T/dT,  dJ_T/dT_e = J_T^{1-P} s_e T_e^{P-1}, spread /8 to nodes
//   2. A^T  ls = -(dRt/du)^T lt         (same coupling as TripleAdjoint)
//   grad_i = dJ_T/dgamma_i|exp + lt^T (dK_t/dg_i) T + ls^T (dA/dg_i) w
//   dJ_T/dgamma_i|exp = (1/P) J_T^{1-P} T_i^P (ds_i/dgamma_i)  with ds/dgamma=-1
//   dK_t/dg_i = dk_i L0_i ; dA/dg_i = dalpha_i M_vel_i.
class ThermalObjectiveAdjoint {
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
        // p-norm exponent for the wall-temperature aggregation.
        double P = 8.0;
    };

    struct Solution {
        Vec w;   // Stokes solution (4*nNodes)
        Vec T;   // nodal temperature (nNodes)
        double J = 0.0;   // J_T
        Vec grad;           // dJ_T/dgamma (nElems)
        Vec termStokes;     // ls^T (dA/dg) w
        Vec termThermal;    // lt^T (dKt/dg) T
        Vec termExplicit;   // dJ_T/dgamma|exp  (via s_e = 1-gamma)
    };

    ThermalObjectiveAdjoint(const Grid3D& grid, const Params& prm,
                            const std::vector<int>& stokesFixedDofs,
                            const std::array<double, 3>& bodyForce,
                            const std::vector<std::uint8_t>& thermalDirMask,
                            const Vec& thermalDirVal, const Vec& Q);

    // Full forward cascade -> J_T (used by the FD oracle).
    double objective(const Vec& gamma) const;

    // Forward + the two discrete adjoint solves + gradient.
    Solution solve(const Vec& gamma) const;

    // Peak nodal speed of the current Stokes solution (diagnostic / Peclet).
    double maxSpeed(const Vec& w) const;

    static int sdof(int node, int c) { return 4 * node + c; }

private:
    double alpha(double g) const;
    double dAlpha(double g) const;

    void buildStokes(const Vec& gamma, SpMat& Afree, Vec& ffree) const;
    SpMat buildThermalFull(const Vec& gamma, const Vec& vel3) const;
    Vec thermalSource() const;                       // int N Q  (full nNodes)
    Vec stokesAdjointRhs(const Vec& T, const Vec& lamT) const;  // (full 4N)

    void forward(const Vec& gamma, Vec& w, Vec& T) const;
    Vec velocity3(const Vec& w) const;               // extract nodal u (3N)
    double computeJT(const Vec& gamma, const Vec& T) const;

    const Grid3D& grid_;
    Params prm_;
    std::array<double, 3> bodyForce_;
    Vec dirValT_;
    std::vector<std::uint8_t> dirMaskT_;
    Vec Q_;

    // Element operators on the unit cube (h = 1).
    H8Element::Mat8 l0_;        // scalar diffusion (= viscous Laplacian block)
    H8Element::Mat8 mvel_;      // velocity mass (Brinkman)
    Eigen::Matrix<double, 32, 32> stokesKe_;  // constant saddle-point element mat

    // Reduced-DOF maps (global -> reduced, -1 if constrained).
    std::vector<int> sMap_;  int nFreeS_ = 0;   // Stokes (4N)
    std::vector<int> tMap_;  int nFreeT_ = 0;   // thermal (N)
};

} // namespace topopt
