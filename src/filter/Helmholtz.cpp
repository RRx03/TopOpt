#include "filter/Helmholtz.hpp"

#include <array>
#include <cmath>
#include <vector>

namespace topopt {

namespace {
// Unit Q4 element matrices, node order [bl, br, tr, tl] (CCW perimeter).
// Consistent mass: diag 4, edge-adjacent 2, diagonal-opposite 1.
constexpr double Me[4][4] = {
    {4, 2, 1, 2}, {2, 4, 2, 1}, {1, 2, 4, 2}, {2, 1, 2, 4},
};
// Diffusion (Laplacian): diag 4, edge-adjacent -1, diagonal-opposite -2.
constexpr double Le[4][4] = {
    {4, -1, -2, -1}, {-1, 4, -1, -2}, {-2, -1, 4, -1}, {-1, -2, -1, 4},
};
} // namespace

Helmholtz::Helmholtz(const Grid2D& grid, double radiusCells) : grid_(grid) {
    const double r = radiusCells / (2.0 * std::sqrt(3.0));
    const double r2 = r * r;

    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(static_cast<size_t>(grid_.nElems()) * 16);

    for (int elx = 0; elx < grid_.nelx(); ++elx) {
        for (int ely = 0; ely < grid_.nely(); ++ely) {
            const auto nodes = grid_.elementNodes(elx, ely);  // [bl,br,tr,tl]
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j) {
                    const double v = r2 * Le[i][j] / 6.0 + Me[i][j] / 36.0;
                    triplets.emplace_back(nodes[i], nodes[j], v);
                }
        }
    }

    KF_ = SpMat(grid_.nNodes(), grid_.nNodes());
    KF_.setFromTriplets(triplets.begin(), triplets.end());
    KF_.makeCompressed();
    solver_.compute(KF_);
}

Helmholtz::Vec Helmholtz::apply(const Vec& x) const {
    // Forward map elements -> nodes: rhs_node += x_e / 4 (integral of N_i).
    Vec rhs = Vec::Zero(grid_.nNodes());
    for (int elx = 0; elx < grid_.nelx(); ++elx) {
        for (int ely = 0; ely < grid_.nely(); ++ely) {
            const double xe = x(grid_.elemId(elx, ely));
            const auto nodes = grid_.elementNodes(elx, ely);
            for (int i = 0; i < 4; ++i) rhs(nodes[i]) += 0.25 * xe;
        }
    }

    const Vec y = solver_.solve(rhs);

    // Back-project nodes -> elements: mean of the 4 nodal values.
    Vec out = Vec::Zero(grid_.nElems());
    for (int elx = 0; elx < grid_.nelx(); ++elx) {
        for (int ely = 0; ely < grid_.nely(); ++ely) {
            const auto nodes = grid_.elementNodes(elx, ely);
            double s = 0.0;
            for (int i = 0; i < 4; ++i) s += y(nodes[i]);
            out(grid_.elemId(elx, ely)) = 0.25 * s;
        }
    }
    return out;
}

} // namespace topopt
