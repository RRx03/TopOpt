#pragma once

#include <vector>

#include <Eigen/Sparse>

#include "core/Grid2D.hpp"

namespace topopt {

// 2D linear-elastic FE solver, bilinear Q4 elements, unit-square cells,
// plane stress. Element stiffness KE0 is for Young's modulus E = 1; the
// assembled K scales each element by its (already interpolated) modulus E_e.
class FEM2D {
public:
    using SpMat = Eigen::SparseMatrix<double>;
    using Vec = Eigen::VectorXd;
    using Mat8 = Eigen::Matrix<double, 8, 8>;

    FEM2D(const Grid2D& grid, double nu);

    // Homogeneous Dirichlet BC: these global DOFs are constrained to 0.
    // Precomputes the free-DOF reduction map.
    void setFixedDofs(const std::vector<int>& fixedDofs);

    // Solve K(E) U = F for the full DOF vector (fixed DOFs returned as 0).
    // E has one entry per element (row-major elemId order).
    Vec solve(const Vec& E, const Vec& F) const;

    // u_e^T KE0 u_e for element (elx, ely): unit-modulus strain energy.
    double elementStrainEnergy(const Vec& U, int elx, int ely) const;

    const Mat8& KE0() const { return ke0_; }
    int nFree() const { return nFree_; }

private:
    const Grid2D& grid_;
    Mat8 ke0_;
    std::vector<int> dofMap_;  // global DOF -> reduced index, or -1 if fixed
    int nFree_ = 0;

    static Mat8 buildKE0(double nu);
};

} // namespace topopt
