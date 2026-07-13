#include "fem/FEM2D.hpp"

#include <Eigen/SparseCholesky>

namespace topopt {

FEM2D::FEM2D(const Grid2D& grid, double nu) : grid_(grid) {
    ke0_ = buildKE0(nu);
}

// Canonical top88 element stiffness matrix (E = 1, unit square, plane stress).
FEM2D::Mat8 FEM2D::buildKE0(double nu) {
    const double k[8] = {
        1.0 / 2.0 - nu / 6.0,    1.0 / 8.0 + nu / 8.0,
        -1.0 / 4.0 - nu / 12.0,  -1.0 / 8.0 + 3.0 * nu / 8.0,
        -1.0 / 4.0 + nu / 12.0,  -1.0 / 8.0 - nu / 8.0,
        nu / 6.0,                1.0 / 8.0 - 3.0 * nu / 8.0,
    };
    // Index pattern of the symmetric 8x8 KE in terms of k[0..7].
    static const int idx[8][8] = {
        {0, 1, 2, 3, 4, 5, 6, 7},
        {1, 0, 7, 6, 5, 4, 3, 2},
        {2, 7, 0, 5, 6, 3, 4, 1},
        {3, 6, 5, 0, 7, 2, 1, 4},
        {4, 5, 6, 7, 0, 1, 2, 3},
        {5, 4, 3, 2, 1, 0, 7, 6},
        {6, 3, 4, 1, 2, 7, 0, 5},
        {7, 2, 1, 4, 3, 6, 5, 0},
    };
    Mat8 ke;
    const double scale = 1.0 / (1.0 - nu * nu);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            ke(i, j) = scale * k[idx[i][j]];
    return ke;
}

void FEM2D::setFixedDofs(const std::vector<int>& fixedDofs) {
    const int nDof = grid_.nDof();
    std::vector<char> fixed(static_cast<size_t>(nDof), 0);
    for (int d : fixedDofs) fixed[static_cast<size_t>(d)] = 1;

    dofMap_.assign(static_cast<size_t>(nDof), -1);
    nFree_ = 0;
    for (int d = 0; d < nDof; ++d)
        if (!fixed[static_cast<size_t>(d)])
            dofMap_[static_cast<size_t>(d)] = nFree_++;
}

FEM2D::Vec FEM2D::solve(const Vec& E, const Vec& F) const {
    const int n = nFree_;

    // Assemble reduced stiffness over free DOFs only (homogeneous Dirichlet).
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(static_cast<size_t>(grid_.nElems()) * 64);

    for (int elx = 0; elx < grid_.nelx(); ++elx) {
        for (int ely = 0; ely < grid_.nely(); ++ely) {
            const double Ee = E(grid_.elemId(elx, ely));
            const auto dofs = grid_.elementDofs(elx, ely);
            for (int a = 0; a < 8; ++a) {
                const int ra = dofMap_[static_cast<size_t>(dofs[a])];
                if (ra < 0) continue;
                for (int b = 0; b < 8; ++b) {
                    const int rb = dofMap_[static_cast<size_t>(dofs[b])];
                    if (rb < 0) continue;
                    triplets.emplace_back(ra, rb, Ee * ke0_(a, b));
                }
            }
        }
    }

    SpMat K(n, n);
    K.setFromTriplets(triplets.begin(), triplets.end());
    K.makeCompressed();

    // Reduce RHS.
    Vec Fr(n);
    for (int d = 0; d < grid_.nDof(); ++d) {
        const int r = dofMap_[static_cast<size_t>(d)];
        if (r >= 0) Fr(r) = F(d);
    }

    Eigen::SimplicialLDLT<SpMat> solver;
    solver.compute(K);
    const Vec Ur = solver.solve(Fr);

    // Scatter back to full DOF vector (fixed DOFs stay 0).
    Vec U = Vec::Zero(grid_.nDof());
    for (int d = 0; d < grid_.nDof(); ++d) {
        const int r = dofMap_[static_cast<size_t>(d)];
        if (r >= 0) U(d) = Ur(r);
    }
    return U;
}

double FEM2D::elementStrainEnergy(const Vec& U, int elx, int ely) const {
    const auto dofs = grid_.elementDofs(elx, ely);
    Eigen::Matrix<double, 8, 1> ue;
    for (int a = 0; a < 8; ++a) ue(a) = U(dofs[a]);
    return ue.transpose() * ke0_ * ue;
}

} // namespace topopt
