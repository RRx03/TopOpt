#include "adjoint/AxiStressAdjoint.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "fem/AxiQ4Element.hpp"

namespace topopt {

namespace {
inline double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
}  // namespace

AxiStressAdjoint::AxiStressAdjoint(const Grid2DAxi& grid, const Material& mat,
                                   const std::vector<int>& fixedDofs,
                                   const Vec& F)
    : grid_(grid), mat_(mat), F_(F), fem_(grid, mat.nu) {
    fem_.setFixedDofs(fixedDofs);
}

AxiStressAdjoint::Vec AxiStressAdjoint::youngModulus(const Vec& rho) const {
    Vec E(rho.size());
    for (Eigen::Index e = 0; e < rho.size(); ++e)
        E(e) = mat_.Emin +
               std::pow(clamp01(rho(e)), mat_.p) * (mat_.E0 - mat_.Emin);
    return E;
}

void AxiStressAdjoint::forward(const Vec& rho, Vec& U) const {
    const Vec E = youngModulus(rho);
    U = fem_.solve(E, F_);
}

double AxiStressAdjoint::stressPNorm(const Vec& rho,
                                     const StressModelAxi& sm) const {
    Vec U;
    forward(rho, U);
    const Vec vm0 = sm.vonMisesSolid(grid_, U);
    const Vec sigma = sm.relaxedStress(rho, vm0);
    return sm.pNorm(sigma);
}

AxiStressAdjoint::StressSolution AxiStressAdjoint::stressPNormGrad(
    const Vec& rho, const StressModelAxi& sm) const {
    StressSolution sol;
    forward(rho, sol.U);

    const double q = sm.qRelax();
    const double P = sm.Pagg();
    const double nu = mat_.nu;
    const auto& V = sm.V();  // symmetric already

    const Vec vm0 = sm.vonMisesSolid(grid_, sol.U);
    const Vec sigma = sm.relaxedStress(rho, vm0);
    const double sigPN = sm.pNorm(sigma);
    sol.J = sigPN;

    const int nE = grid_.nElems();
    const double vmFloor = 1e-12;  // guard vm0 ~ 0 in (1/vm0) S^T V s

    // dJ/dsigma_e = sigma_PN^(1-P) sigma_e^(P-1).
    Vec dJdsig(nE);
    for (int e = 0; e < nE; ++e)
        dJdsig(e) = std::pow(sigPN, 1.0 - P) * std::pow(sigma(e), P - 1.0);

    // dJ/dU = sum_e (dJ/dsigma_e)(dsigma_e/du_e) scattered to global DOFs,
    // with dsigma_e/du_e = rho_e^q (1/vm0_e) S0ax_e^T Vax s_e.
    // Explicit term  dJ/drho_i|exp = (dJ/dsigma_i) q rho_i^(q-1) vm0_i.
    Vec dJdU = Vec::Zero(grid_.nDof());
    sol.termExplicit = Vec::Zero(nE);
    for (int ej = 0; ej < grid_.nz(); ++ej)
        for (int ei = 0; ei < grid_.nr(); ++ei) {
            const int eid = grid_.elemId(ei, ej);
            std::array<double, 4> rn, zn;
            StressModelAxi::cornerCoords(grid_, ei, ej, rn, zn);
            const auto S = AxiQ4Element::stressMatrix(rn, zn, nu);
            const auto dofs = grid_.elementDofs(ei, ej);
            AxiQ4Element::Vec8 ue;
            for (int a = 0; a < 8; ++a)
                ue(a) = sol.U(dofs[static_cast<size_t>(a)]);

            const double r = clamp01(rho(eid));
            const double rq = std::pow(r, q);

            if (vm0(eid) > vmFloor) {
                const AxiQ4Element::Vec4 s = S * ue;
                const AxiQ4Element::Vec8 dsig_du =
                    (rq / vm0(eid)) * (S.transpose() * (V * s));
                const double w = dJdsig(eid);
                for (int a = 0; a < 8; ++a)
                    dJdU(dofs[static_cast<size_t>(a)]) += w * dsig_du(a);
            }

            // rho^(q-1) diverges for q<1 as rho->0; floor rho (LL-008).
            const double rEps = std::max(r, 1e-9);
            sol.termExplicit(eid) =
                dJdsig(eid) * q * std::pow(rEps, q - 1.0) * vm0(eid);
        }

    // Elastic adjoint: K(E) lam = -dJ/dU.
    const Vec E = youngModulus(rho);
    const Vec lam = fem_.solve(E, -dJdU);

    // Adjoint term: lam_i^T (dE_i KE0ax_i) u_i. KE0ax_i is the E=1 element
    // stiffness, recomputed PER ELEMENT (axisymmetry: depends on r).
    sol.termAdjoint = Vec::Zero(nE);
    for (int ej = 0; ej < grid_.nz(); ++ej)
        for (int ei = 0; ei < grid_.nr(); ++ei) {
            const int eid = grid_.elemId(ei, ej);
            std::array<double, 4> rn, zn;
            StressModelAxi::cornerCoords(grid_, ei, ej, rn, zn);
            const auto ke0 = AxiQ4Element::stiffness(rn, zn, nu);  // E=1
            const auto dofs = grid_.elementDofs(ei, ej);
            AxiQ4Element::Vec8 ue, le;
            for (int a = 0; a < 8; ++a) {
                ue(a) = sol.U(dofs[static_cast<size_t>(a)]);
                le(a) = lam(dofs[static_cast<size_t>(a)]);
            }
            const double r = clamp01(rho(eid));
            const double dE =
                mat_.p * std::pow(r, mat_.p - 1.0) * (mat_.E0 - mat_.Emin);
            sol.termAdjoint(eid) = dE * (le.transpose() * ke0 * ue)(0, 0);
        }

    sol.grad = sol.termExplicit + sol.termAdjoint;
    return sol;
}

} // namespace topopt
