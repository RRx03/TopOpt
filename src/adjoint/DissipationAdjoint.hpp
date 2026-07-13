#pragma once

#include <array>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Sparse>

#include "core/Grid3D.hpp"
#include "fem/H8Element.hpp"

namespace topopt {

// Stokes dissipated-power objective and its discrete adjoint gradient
// (Borrvall-Petersson fluid TO gate), CPU double precision, self-contained
// (Eigen direct solves, unit-cube H8 cells, h = 1). The Stokes-Brinkman
// operator A(gamma) and the objective matrix H are assembled inside this class
// from the SAME element matrices, so the discrete adjoint differentiates
// EXACTLY the operators seen by the finite-difference oracle.
//
// Primal:  A(gamma) w = f,  w = (u,p), 4 DOF/node,
//   A = viscous(mu*L) + B/Bt(pressure) + PSPG(-tau*L) + alpha(gamma)*M_vel
//   alpha(gamma) = amax + (amin-amax) g (1+q)/(g+q)   (Borrvall-Petersson).
//   Nonzero Dirichlet (imposed inlet velocity) is handled by a lift.
//
// Objective (dissipated power):
//   Phi = 1/2 ∫ mu |grad u|^2 + 1/2 ∫ alpha(gamma) |u|^2 = 1/2 w^T H w,
// where H has only the velocity block A_uu = mu*L + alpha(gamma)*M_vel
// (pressure block zero -> dPhi/dp = 0).
//
// Gradient (adjoint):
//   dPhi/dg_i = 1/2 dalpha_i (u^T M_vel u)_i        (explicit, via H)
//             + lambda^T (dA/dg_i) w,  dA/dg_i = dalpha_i M_vel_i,
//   A^T lambda = -H w   (velocity rows = -A_uu u, pressure rows = 0).
// Quasi self-adjoint: lambda_u ~ -u (reported as a coherence check).
class DissipationAdjoint {
public:
    using Vec = Eigen::VectorXd;
    using SpMat = Eigen::SparseMatrix<double>;

    struct Params {
        double mu = 1.0;
        double alphaStab = 1.0 / 12.0;  // PSPG coefficient (as in StokesSolver)
        double alphaMax = 0.0;          // Brinkman inverse-permeability bounds
        double alphaMin = 0.0;
        double qBrink = 0.1;            // Borrvall-Petersson convexity knob
    };

    struct Solution {
        Vec w;             // Stokes solution (4*nNodes)
        Vec lambda;        // adjoint (4*nNodes)
        double Phi = 0.0;  // dissipated power
        Vec grad;          // dPhi/dgamma (nElems)
        Vec termExplicit;  // 1/2 dalpha (u^T M_vel u)_e
        Vec termAdjoint;   // lambda^T (dA/dg) w
        double selfAdjResidual = 0.0;  // ||lambda_u + u|| / ||u||
    };

    DissipationAdjoint(const Grid3D& grid, const Params& prm,
                       const std::vector<int>& fixedDofs,
                       const std::array<double, 3>& bodyForce,
                       const Vec& dirichletVal);

    // Forward Stokes-Brinkman solve + objective (used by the FD oracle).
    double objective(const Vec& gamma) const;

    // Forward + adjoint solve + gradient.
    Solution solve(const Vec& gamma) const;

    static int sdof(int node, int c) { return 4 * node + c; }

private:
    double alpha(double g) const;
    double dAlpha(double g) const;

    // Full (unreduced) Stokes-Brinkman operator and body-force load.
    void buildStokesFull(const Vec& gamma, SpMat& Afull, Vec& fFull) const;
    // Forward solve with Dirichlet lift -> full 4N solution.
    Vec forward(const Vec& gamma) const;
    // H w = objective-matrix action (velocity block only), full 4N.
    Vec applyH(const Vec& gamma, const Vec& w) const;

    const Grid3D& grid_;
    Params prm_;
    std::array<double, 3> bodyForce_;
    Vec dirVal_;   // prescribed velocity values (4N; nonzero only at fixed DOFs)

    H8Element::Mat8 l0_;    // scalar Laplacian (viscous block per component)
    H8Element::Mat8 mvel_;  // velocity mass (Brinkman + objective quadratic)
    Eigen::Matrix<double, 32, 32> stokesKe_;  // constant saddle-point element mat

    std::vector<int> sMap_;  // global 4N DOF -> reduced index (-1 if fixed)
    int nFree_ = 0;
};

} // namespace topopt
