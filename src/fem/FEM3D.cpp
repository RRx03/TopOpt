#include "fem/FEM3D.hpp"

#include <Eigen/SparseCholesky>

namespace topopt {

FEM3D::FEM3D(const Grid3D& grid, double nu) : grid_(grid) {
    ke0_ = H8Element::stiffness(nu);
}

void FEM3D::setFixedDofs(const std::vector<int>& fixedDofs) {
    const int nDof = grid_.nDof();
    std::vector<char> fixed(static_cast<size_t>(nDof), 0);
    for (int d : fixedDofs) fixed[static_cast<size_t>(d)] = 1;

    dofMap_.assign(static_cast<size_t>(nDof), -1);
    nFree_ = 0;
    for (int d = 0; d < nDof; ++d)
        if (!fixed[static_cast<size_t>(d)])
            dofMap_[static_cast<size_t>(d)] = nFree_++;
}

FEM3D::Vec FEM3D::solve(const Vec& E, const Vec& F) const {
    const int n = nFree_;
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(static_cast<size_t>(grid_.nElems()) * 24 * 24);

    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const double Ee = E(grid_.elemId(ex, ey, ez));
                const auto dofs = grid_.elementDofs(ex, ey, ez);
                for (int a = 0; a < 24; ++a) {
                    const int ra = dofMap_[static_cast<size_t>(dofs[static_cast<size_t>(a)])];
                    if (ra < 0) continue;
                    for (int b = 0; b < 24; ++b) {
                        const int rb = dofMap_[static_cast<size_t>(dofs[static_cast<size_t>(b)])];
                        if (rb < 0) continue;
                        triplets.emplace_back(ra, rb, Ee * ke0_(a, b));
                    }
                }
            }

    SpMat K(n, n);
    K.setFromTriplets(triplets.begin(), triplets.end());
    K.makeCompressed();

    Vec Fr(n);
    for (int d = 0; d < grid_.nDof(); ++d) {
        const int r = dofMap_[static_cast<size_t>(d)];
        if (r >= 0) Fr(r) = F(d);
    }

    Eigen::SimplicialLDLT<SpMat> solver;
    solver.compute(K);
    const Vec Ur = solver.solve(Fr);

    Vec U = Vec::Zero(grid_.nDof());
    for (int d = 0; d < grid_.nDof(); ++d) {
        const int r = dofMap_[static_cast<size_t>(d)];
        if (r >= 0) U(d) = Ur(r);
    }
    return U;
}

double FEM3D::elementStrainEnergy(const Vec& U, int ex, int ey, int ez) const {
    const auto dofs = grid_.elementDofs(ex, ey, ez);
    Eigen::Matrix<double, 24, 1> ue;
    for (int a = 0; a < 24; ++a) ue(a) = U(dofs[static_cast<size_t>(a)]);
    return ue.transpose() * ke0_ * ue;
}

} // namespace topopt
