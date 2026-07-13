#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <Eigen/Sparse>

#include "core/Grid3D.hpp"

namespace topopt {

// Steady conjugate-heat-transfer temperature solver (CPU, double precision) on
// a structured H8 grid, scalar Q1 trilinear field (1 DOF/node). Solves the
// stationary advection-diffusion equation
//
//     −∇·(k(γ)∇T) + u·∇T = Q
//
// with SUPG stabilisation of the advective term. Cube cells of physical side h.
//
// Weak form (test function w):
//   Diffusion : K_d = ∫ k ∇w·∇T                        (symmetric, "conduction")
//   Advection : C_a = ∫ w (u·∇T)                        (NON-symmetric)
//   SUPG      : S   = ∫ τ (u·∇w)(u·∇T)  and  −∫ τ (u·∇w) Q on the RHS
//   RHS       : F   = ∫ w Q  (+ SUPG source term)
// with the classic optimal streamline parameter
//   τ = h/(2|u|) · (coth(Pe_e) − 1/Pe_e),  Pe_e = |u| h / (2k).
// (The residual used for SUPG is (u·∇T − Q): the diffusion term is dropped, as
//  ∇·(k∇T) is second order and negligible for trilinear elements — the standard
//  linear-element SUPG choice.) Without SUPG the Galerkin scheme oscillates once
// the element Péclet number exceeds ~1; SUPG removes the wiggles.
//
// The global operator K_d + C_a + S is non-symmetric, so it is factored with a
// direct sparse LU (Eigen::SparseLU). Dirichlet BCs support non-zero prescribed
// values (T(0)=0, T(L)=1 for the analytic oracle) via a lift.
class CHTSolver {
public:
    using Vec = Eigen::VectorXd;
    using SpMat = Eigen::SparseMatrix<double>;

    CHTSolver(const Grid3D& grid, double h);

    // Solve the advection-diffusion problem.
    //   kElem    : per-element conductivity k (length nElems), evaluated at the
    //              Gauss points as constant per element.
    //   velocity : nodal velocity field, length 3*nNodes, layout 3*node + c;
    //              interpolated to the Gauss points. Uniform for the oracle.
    //   Q        : nodal heat source (length nNodes), zeros for the oracle.
    //   dirMask  : per-node Dirichlet mask (1 = prescribed).
    //   dirVal   : per-node prescribed temperature (used where dirMask=1).
    //   supg     : enable SUPG stabilisation.
    // Returns the nodal temperature (length nNodes); Dirichlet nodes hold dirVal.
    Vec solve(const std::vector<double>& kElem, const Vec& velocity,
              const Vec& Q, const std::vector<std::uint8_t>& dirMask,
              const Vec& dirVal, bool supg) const;

    int nNodes() const { return grid_.nNodes(); }
    int nElems() const { return grid_.nElems(); }

    // Two-phase conductivity interpolation k(γ) = k_s + (k_f − k_s)·γ, γ in
    // [0,1] (clamped, LL-008). Both phases conduct; the fluid advects in addition.
    static double kInterp(double gamma, double ks, double kf) {
        const double g = std::clamp(gamma, 0.0, 1.0);
        return ks + (kf - ks) * g;
    }

private:
    const Grid3D& grid_;
    double h_;
};

} // namespace topopt
