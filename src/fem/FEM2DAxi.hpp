#pragma once

#include <vector>

#include <Eigen/Sparse>

#include "core/Grid2DAxi.hpp"
#include "fem/AxiQ4Element.hpp"

namespace topopt {

// 2D axisymmetric linear-elastic reference solver (CPU, double precision).
// Bilinear Q4 elements in the (r,z) half-plane, direct factorisation
// (SimplicialLDLT). Validated against the thick-cylinder Lame solution.
class FEM2DAxi {
public:
    using SpMat = Eigen::SparseMatrix<double>;
    using Vec = Eigen::VectorXd;

    FEM2DAxi(const Grid2DAxi& grid, double nu);

    void setFixedDofs(const std::vector<int>& fixedDofs);

    // Solve K(E) U = F. E has one entry per element (elemId order).
    Vec solve(const Vec& E, const Vec& F) const;

    // Consistent radial nodal loads from an internal pressure p_i on r=a.
    // The 2*pi factor is omitted (consistent with the stiffness assembly).
    // Tributary z-weight: 0.5 at the z-extremities, 1 otherwise.
    Vec pressureLoadInner(double p_i) const;

    // Consistent nodal load from an internal pressure acting on the (possibly
    // profiled) inner face r = rNode(0,j). pAtRow gives the pressure at each
    // z-node (size nzn()). The face normal (into the wall, +r side) is used, so
    // a slanted bore also picks up an axial load component. For a rectangular
    // grid with constant pressure this reduces EXACTLY to pressureLoadInner
    // (the sanity guardrail asserts bit-equality). 2*pi is omitted as elsewhere.
    Vec pressureLoadInnerProfiled(const std::vector<double>& pAtRow) const;

    // Recovered stress [sigma_r, sigma_z, sigma_theta, tau_rz] at element
    // centroids, one Vec4 per element (elemId order).
    std::vector<AxiQ4Element::Vec4> elementStress(const Vec& E,
                                                  const Vec& U) const;

    int nFree() const { return nFree_; }

private:
    const Grid2DAxi& grid_;
    double nu_;
    std::vector<int> dofMap_;  // global DOF -> reduced index, or -1 if fixed
    int nFree_ = 0;

    // Per-element corner coordinates in local node order.
    void elementCoords(int ei, int ej, std::array<double, 4>& r_nodes,
                       std::array<double, 4>& z_nodes) const;
};

} // namespace topopt
