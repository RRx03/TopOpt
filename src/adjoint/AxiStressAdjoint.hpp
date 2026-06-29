#pragma once

#include <vector>

#include <Eigen/Core>

#include "core/Grid2DAxi.hpp"
#include "fem/FEM2DAxi.hpp"
#include "topopt/StressModelAxi.hpp"

namespace topopt {

// Discrete adjoint of the axisymmetric stress p-norm sigma_PN, elastic only
// (no thermal block). Forward: K(E) U = F with F a fixed internal-pressure
// load and E_e = Emin + rho_e^p (E0 - Emin). The gradient dsigma_PN/drho has
//   - an explicit relaxation term  (dsigma_PN/dsigma_i) q rho_i^(q-1) vm0_i,
//   - an adjoint term  lam_i^T (dE_i KE0ax_i) u_i  carrying dsigma_PN/dU,
// where lam solves  K(E) lam = -dsigma_PN/dU.
//
// CRITICAL (axisymmetry): the element stiffness depends on r, so the E=1
// element matrix KE0ax_i is PROPER to each element (recomputed per element),
// unlike the shared KE0 of a Cartesian 3D mesh.
class AxiStressAdjoint {
public:
    using Vec = Eigen::VectorXd;

    struct Material {
        double E0 = 1.0;
        double Emin = 1e-4;
        double p = 3.0;   // SIMP exponent
        double nu = 0.3;
    };

    struct StressSolution {
        double J = 0.0;       // sigma_PN
        Vec U;                // forward displacement
        Vec grad;             // total gradient dsigma_PN/drho
        Vec termExplicit;     // explicit rho^q-relaxation term
        Vec termAdjoint;      // lam^T (dK/drho) U term (carries dsigma_PN/dU)
    };

    AxiStressAdjoint(const Grid2DAxi& grid, const Material& mat,
                     const std::vector<int>& fixedDofs, const Vec& F);

    // SIMP Young's modulus per element.
    Vec youngModulus(const Vec& rho) const;

    // Forward solve K(E(rho)) U = F.
    void forward(const Vec& rho, Vec& U) const;

    // Scalar objective sigma_PN (re-solves the forward).
    double stressPNorm(const Vec& rho, const StressModelAxi& sm) const;

    // Full gradient of sigma_PN by the elastic adjoint.
    StressSolution stressPNormGrad(const Vec& rho,
                                   const StressModelAxi& sm) const;

private:
    const Grid2DAxi& grid_;
    Material mat_;
    Vec F_;
    FEM2DAxi fem_;
};

} // namespace topopt
