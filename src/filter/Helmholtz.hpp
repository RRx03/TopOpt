#pragma once

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>

#include "core/Grid2D.hpp"

namespace topopt {

// PDE (Helmholtz) density filter, Lazarov & Sigmund 2011:
//     (-r^2 grad^2 + 1) rho_tilde = rho,  homogeneous Neumann.
// Element densities are mapped to nodes, smoothed by the FE solve, then
// mapped back to elements. The resulting operator H is symmetric, so the
// same apply() filters both the density field and the sensitivities.
//
// The length-scale r is derived from a classic filter radius R (in cells)
// via the standard mapping r = R / (2*sqrt(3)).
class Helmholtz {
public:
    using SpMat = Eigen::SparseMatrix<double>;
    using Vec = Eigen::VectorXd;

    Helmholtz(const Grid2D& grid, double radiusCells);

    // H * x for a per-element field x (returns a per-element field).
    Vec apply(const Vec& x) const;

private:
    const Grid2D& grid_;
    SpMat KF_;
    Eigen::SimplicialLDLT<SpMat> solver_;
};

} // namespace topopt
