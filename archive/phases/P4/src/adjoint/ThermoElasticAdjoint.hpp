#pragma once

#include <vector>

#include <Eigen/Core>

#include "core/Grid3D.hpp"
#include "fem/FEM3D.hpp"
#include "fem/H8Element.hpp"
#include "topopt/StressModel.hpp"

namespace topopt {

// Discrete two-block thermo-elastic adjoint (one-way coupling T -> U), CPU
// double precision (Eigen direct solves) — the validation oracle for Phase 4.
//
// Forward problem:
//   Thermal :  K_t(rho) T = Q                 (Q fixed, rho-independent)
//   Elastic :  K_e(rho) U = F_mech + F_th(rho, T)
//   F_th = sum_e E_e(rho) alpha C_e (T_e - Tref),  C_e = H8Element::thermalCoupling
// SIMP interpolation (clamped before pow, LL-008):
//   E_e = Emin + rho_e^p (E0 - Emin)     (elastic)
//   k_e = kmin + rho_e^q (k0 - kmin)     (thermal conductivity)
// Objective: J = L^T U with L = F_mech (fixed linear functional).
//
// Adjoint (Lagrangian L = J + lam_e^T R_e + lam_t^T R_t):
//   1. K_e lam_e = -L                                   (elastic adjoint)
//   2. K_t lam_t = G^T lam_e,  G = dF_th/dT             (thermal adjoint, coupled)
//      per element: (G^T lam_e)_e = E_e alpha C_e^T lam_e_local  (8-vector)
//   3. dJ/drho_i = lam_e^T[ (dK_e/drho_i) U - dF_th/drho_i ]
//                + lam_t^T (dK_t/drho_i) T
//      with dK_e/drho_i = dE_i KE0,  dF_th/drho_i = dE_i alpha C_e (T_i - Tref),
//           dK_t/drho_i = dk_i L0.
class ThermoElasticAdjoint {
public:
    using Vec = Eigen::VectorXd;

    struct Material {
        double E0 = 1.0;
        double Emin = 1e-4;
        double p = 3.0;
        double k0 = 1.0;
        double kmin = 1e-4;
        double q = 3.0;
        double alpha = 1e-3;
        double Tref = 0.0;
        double nu = 0.3;
    };

    struct Solution {
        Vec T;          // nodal temperatures (nNodes)
        Vec U;          // nodal displacements (nDof)
        double J = 0.0; // objective L^T U
        Vec grad;       // dJ/drho (nElems)
        // Per-element decomposition of the gradient (for inspection / report).
        Vec termElastic;   // lam_e^T dE KE0 U
        Vec termThermalLoad;  // -lam_e^T dE alpha C (T - Tref)
        Vec termConduction;   // lam_t^T dk L0 T
    };

    ThermoElasticAdjoint(const Grid3D& grid, const Material& mat,
                         const std::vector<int>& elasticFixedDofs,
                         const std::vector<int>& thermalFixedNodes,
                         const Vec& Fmech, const Vec& Q);

    // Forward solve only -> J = F_mech^T U (used by the finite-difference oracle).
    double objective(const Vec& rho) const;

    // Forward + discrete adjoint gradient.
    Solution solve(const Vec& rho) const;

    // --- Stress p-norm objective (same forward, same 2 adjoint solves) ---------
    // J = sigma_PN = (sum_e sigma_e^P)^(1/P), sigma_e = rho_e^q * vm0_e,
    // vm0_e = sqrt(s_e^T V s_e), s_e = S0 u_e (S0 = StressModel::S0, V = vonMisesForm).
    struct StressSolution {
        Vec T;
        Vec U;
        double J = 0.0;     // sigma_PN
        Vec grad;           // dJ/drho (nElems)
        Vec termExplicit;   // dJ/drho|_explicit (via rho^q relaxation)
        Vec termDjDu;       // contribution propagated through dJ/dU (elastic adjoint)
        Vec termElastic;    // lam_e^T dE KE0 U
        Vec termThermalLoad;  // -lam_e^T dE alpha C (T - Tref)
        Vec termConduction;   // lam_t^T dk L0 T
    };

    // Forward solve -> sigma_PN only (used by the finite-difference oracle).
    double stressPNorm(const Vec& rho, const StressModel& sm) const;

    // Forward + discrete adjoint gradient of sigma_PN.
    StressSolution stressPNormGrad(const Vec& rho, const StressModel& sm) const;

private:
    Vec youngModulus(const Vec& rho) const;
    Vec conductivity(const Vec& rho) const;
    Vec thermalLoad(const Vec& Evec, const Vec& T) const;
    Vec solveThermal(const Vec& kvec, const Vec& rhs) const;
    void forward(const Vec& rho, Vec& T, Vec& U) const;

    // RHS of the thermal adjoint: g = G^T lam_e (G = dF_th/dT), assembled by node.
    Vec thermalAdjointRhs(const Vec& Evec, const Vec& lamE) const;

    // The three rho-derivative terms shared by every objective: given the state
    // (U, T) and adjoints (lam_e, lam_t), fill the per-element contributions.
    void hereditaryGradient(const Vec& rho, const Vec& U, const Vec& T,
                            const Vec& lamE, const Vec& lamT, Vec& termElastic,
                            Vec& termThermalLoad, Vec& termConduction) const;

    const Grid3D& grid_;
    Material mat_;
    Vec Fmech_;
    Vec Q_;
    FEM3D fem_;
    H8Element::Mat24 ke0_;
    H8Element::Mat8 l0_;
    H8Element::Mat24x8 cth_;
    std::vector<int> thermalDofMap_;  // node -> reduced index, or -1 if Dirichlet
    int nFreeT_ = 0;
};

} // namespace topopt
