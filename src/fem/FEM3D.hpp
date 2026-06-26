#pragma once

#include <vector>

#include <Eigen/Sparse>

#include "core/Grid3D.hpp"
#include "fem/H8Element.hpp"

namespace topopt {

// 3D linear-elastic FE reference solver (CPU), trilinear H8 elements,
// unit-cube cells. Direct factorisation (SimplicialLDLT) like Phase 1.
// Used as ground truth to validate the GPU matrix-free CG solver.
class FEM3D {
public:
    using SpMat = Eigen::SparseMatrix<double>;
    using Vec = Eigen::VectorXd;
    using Mat24 = H8Element::Mat24;

    FEM3D(const Grid3D& grid, double nu);

    void setFixedDofs(const std::vector<int>& fixedDofs);

    // Solve K(E) U = F. E has one entry per element (elemId order).
    Vec solve(const Vec& E, const Vec& F) const;

    // u_e^T KE0 u_e for an element: unit-modulus strain energy.
    double elementStrainEnergy(const Vec& U, int ex, int ey, int ez) const;

    const Mat24& KE0() const { return ke0_; }
    int nFree() const { return nFree_; }

private:
    const Grid3D& grid_;
    Mat24 ke0_;
    std::vector<int> dofMap_;  // global DOF -> reduced index, or -1 if fixed
    int nFree_ = 0;
};

} // namespace topopt
