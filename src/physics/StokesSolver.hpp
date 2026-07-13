#pragma once

#include <algorithm>
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
//
// Brinkman penalization (Phase 5): the momentum equation gains a reaction term
//   −μ∇²u + ∇p + α(γ)u = f,
// with a per-element design field γ (γ=1 fluid, α≈0 ; γ=0 solid, α=α_max) and
// the convex Borrvall-Petersson interpolation
//   α(γ) = α_max + (α_min − α_max)·γ·(1+q)/(γ+q).
// This adds ∫ α(γ) u·v to block A, i.e. per element α(γ_e)·M_vel where M_vel is
// the (constant) element velocity mass matrix. Because α varies per element,
// the uniform ke_ is reused for the constant part and the mass term is added
// element-by-element at assembly. α_max=0 (default) recovers pure Stokes.
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

    // Brinkman variant: gamma is the per-element design field (length nElems),
    // gamma=1 fluid / gamma=0 solid. Adds ∫ α(γ_e) u·v to the momentum block.
    // nodalLoad may be Vec::Zero(nDofTotal()) if unused.
    Vec solve(const std::array<double, 3>& bodyForce, const Vec& nodalLoad,
              const Vec& gamma) const;

    // Set Brinkman interpolation parameters. alphaMax=0 (default) -> pure Stokes
    // regardless of gamma. q is the convexity/penalty knob (~0.1). gamma is
    // clamped to [0,1] before interpolation (LL-008).
    void setBrinkman(double alphaMax, double alphaMin = 0.0, double q = 0.1) {
        alphaMax_ = alphaMax;
        alphaMin_ = alphaMin;
        q_ = q;
    }

    // Borrvall-Petersson inverse-permeability interpolation α(γ).
    double alpha(double gamma) const {
        const double g = std::clamp(gamma, 0.0, 1.0);
        return alphaMax_ + (alphaMin_ - alphaMax_) * g * (1.0 + q_) / (g + q_);
    }

    static int dof(int node, int c) { return 4 * node + c; }
    int nNodes() const { return grid_.nNodes(); }
    int nDofTotal() const { return 4 * grid_.nNodes(); }
    int nFree() const { return nFree_; }
    double tau() const { return alphaStab_ * h_ * h_ / mu_; }

private:
    using Mat8 = Eigen::Matrix<double, 8, 8>;

    // Shared assembly/solve. gamma != nullptr enables the per-element Brinkman
    // mass term α(γ_e)·M_vel.
    Vec assembleAndSolve(const std::array<double, 3>& bodyForce,
                         const Vec& nodalLoad, const Vec* gamma) const;

    const Grid3D& grid_;
    double mu_;
    double h_;
    double alphaStab_;
    Mat32 ke_;                       // element saddle-point matrix (uniform h)
    Mat8 me_;                        // element velocity mass matrix ∫ N_a N_b dV
    std::array<double, 8> sLoad_;    // ∫ N_a dV per node (body-force weights)
    std::vector<int> dofMap_;        // global DOF -> reduced index, -1 if fixed
    int nFree_ = 0;
    double alphaMax_ = 0.0;          // Brinkman inverse-permeability bounds
    double alphaMin_ = 0.0;
    double q_ = 0.1;
};

} // namespace topopt
