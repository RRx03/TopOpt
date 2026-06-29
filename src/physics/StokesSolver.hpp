#pragma once

#include <array>
#include <vector>

#include <Eigen/Sparse>

#include "core/Grid3D.hpp"

namespace topopt {

// Incompressible Stokes solver (CPU, double precision), structured H8 grid,
// equal-order Q1-Q1 elements with Brezzi-Pitkaranta / PSPG pressure
// stabilisation (ADR-017). 4 DOF per node: (u_x, u_y, u_z, p), local layout
// dof = 4*node + c, c in {0,1,2} velocity, c = 3 pressure.
//
// Weak form (unit-cube cells of physical side h, viscosity mu):
//   A   = ∫ mu ∇u : ∇v            (vector Laplacian, block-diagonal per comp)
//   Bᵀ  = −∫ p (∇·v)              (pressure -> momentum)
//   B   = −∫ q (∇·u)              (continuity)
//   C   = ∫ τ ∇q · ∇p,  τ = α_stab h²/μ   (PSPG, kills the inf-sup
//                                           checkerboard mode, LL-LIT-002)
// Saddle-point system  [[A, Bᵀ],[B, −C]] [u; p] = [f; 0], solved in direct
// double precision with Eigen::SparseLU (handles the indefinite operator).
// The PSPG consistency force (−τ∫∇q·f) is zero for this Laplacian variant when
// the exact solution is divergence-free, so the RHS pressure block stays [0].
class StokesSolver {
public:
    using SpMat = Eigen::SparseMatrix<double>;
    using Vec = Eigen::VectorXd;
    using Mat32 = Eigen::Matrix<double, 32, 32>;

    StokesSolver(const Grid3D& grid, double mu, double h, double alphaStab);

    // Fix DOFs to zero (no-slip / slip velocity, pressure datum). Global
    // 4-DOF indexing: use dof(node, c).
    void setFixedDofs(const std::vector<int>& fixedDofs);

    // Solve with a constant body force f = (fx, fy, fz). Returns the full
    // solution (length 4*nNodes), layout dof = 4*node + c.
    Vec solve(const std::array<double, 3>& bodyForce) const;

    // Same, plus an explicit nodal RHS contribution (length 4*nNodes), e.g. the
    // consistent boundary-traction load ∫_Γ t·v for an imposed-traction (open /
    // inlet-outlet) face. Added to the RHS before reduction.
    Vec solve(const std::array<double, 3>& bodyForce,
              const Vec& nodalLoad) const;

    static int dof(int node, int c) { return 4 * node + c; }
    int nNodes() const { return grid_.nNodes(); }
    int nDofTotal() const { return 4 * grid_.nNodes(); }
    int nFree() const { return nFree_; }
    double tau() const { return alphaStab_ * h_ * h_ / mu_; }

private:
    const Grid3D& grid_;
    double mu_;
    double h_;
    double alphaStab_;
    Mat32 ke_;                       // element saddle-point matrix (uniform h)
    std::array<double, 8> sLoad_;    // ∫ N_a dV per node (body-force weights)
    std::vector<int> dofMap_;        // global DOF -> reduced index, -1 if fixed
    int nFree_ = 0;
};

} // namespace topopt
