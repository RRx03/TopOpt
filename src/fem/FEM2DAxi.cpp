#include "fem/FEM2DAxi.hpp"

#include <cmath>

#include <Eigen/SparseCholesky>

namespace topopt {

FEM2DAxi::FEM2DAxi(const Grid2DAxi& grid, double nu) : grid_(grid), nu_(nu) {}

void FEM2DAxi::setFixedDofs(const std::vector<int>& fixedDofs) {
    const int nDof = grid_.nDof();
    std::vector<char> fixed(static_cast<size_t>(nDof), 0);
    for (int d : fixedDofs) fixed[static_cast<size_t>(d)] = 1;

    dofMap_.assign(static_cast<size_t>(nDof), -1);
    nFree_ = 0;
    for (int d = 0; d < nDof; ++d)
        if (!fixed[static_cast<size_t>(d)])
            dofMap_[static_cast<size_t>(d)] = nFree_++;
}

void FEM2DAxi::elementCoords(int ei, int ej, std::array<double, 4>& r_nodes,
                             std::array<double, 4>& z_nodes) const {
    for (int dj = 0; dj < 2; ++dj)
        for (int di = 0; di < 2; ++di) {
            const size_t l = static_cast<size_t>(di + 2 * dj);
            r_nodes[l] = grid_.rNode(ei + di, ej + dj);
            z_nodes[l] = grid_.z(ej + dj);
        }
}

FEM2DAxi::Vec FEM2DAxi::solve(const Vec& E, const Vec& F) const {
    const int n = nFree_;
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(static_cast<size_t>(grid_.nElems()) * 8 * 8);

    for (int ej = 0; ej < grid_.nz(); ++ej)
        for (int ei = 0; ei < grid_.nr(); ++ei) {
            std::array<double, 4> rn, zn;
            elementCoords(ei, ej, rn, zn);
            const double Ee = E(grid_.elemId(ei, ej));
            const auto ke = AxiQ4Element::stiffness(rn, zn, nu_);
            const auto dofs = grid_.elementDofs(ei, ej);
            for (int a = 0; a < 8; ++a) {
                const int ra = dofMap_[static_cast<size_t>(dofs[static_cast<size_t>(a)])];
                if (ra < 0) continue;
                for (int b = 0; b < 8; ++b) {
                    const int rb = dofMap_[static_cast<size_t>(dofs[static_cast<size_t>(b)])];
                    if (rb < 0) continue;
                    triplets.emplace_back(ra, rb, Ee * ke(a, b));
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

FEM2DAxi::Vec FEM2DAxi::pressureLoadInner(double p_i) const {
    Vec F = Vec::Zero(grid_.nDof());
    const double a = grid_.a();
    const double hz = grid_.hz();
    for (int j = 0; j <= grid_.nz(); ++j) {
        const double w = (j == 0 || j == grid_.nz()) ? 0.5 : 1.0;
        const int node = grid_.nodeId(0, j);
        F(2 * node + 0) += p_i * a * hz * w;  // radial DOF
    }
    return F;
}

FEM2DAxi::Vec FEM2DAxi::pressureLoadInnerProfiled(
    const std::vector<double>& pAtRow) const {
    Vec F = Vec::Zero(grid_.nDof());
    for (int j = 0; j < grid_.nz(); ++j) {
        // Inner-face edge between nodes (0,j) and (0,j+1).
        const double rA = grid_.rNode(0, j), zA = grid_.z(j);
        const double rB = grid_.rNode(0, j + 1), zB = grid_.z(j + 1);
        const double dr = rB - rA, dz = zB - zA;
        const double L = std::sqrt(dr * dr + dz * dz);
        if (L <= 0.0) continue;
        // Outward normal into the wall (+r side): (dz, -dr)/L. For a vertical
        // edge (dr=0) this is (1,0), i.e. purely radial, matching Lame.
        const double nr = dz / L, nz = -dr / L;
        const int nodeA = grid_.nodeId(0, j), nodeB = grid_.nodeId(0, j + 1);
        // Consistent axisymmetric edge load: each endpoint gets p*r*(L/2)*n,
        // with its own nodal pressure and radius (2*pi omitted, r-weight kept).
        const double cA = pAtRow[static_cast<size_t>(j)] * rA * 0.5 * L;
        const double cB = pAtRow[static_cast<size_t>(j + 1)] * rB * 0.5 * L;
        F(2 * nodeA + 0) += cA * nr;
        F(2 * nodeA + 1) += cA * nz;
        F(2 * nodeB + 0) += cB * nr;
        F(2 * nodeB + 1) += cB * nz;
    }
    return F;
}

std::vector<AxiQ4Element::Vec4> FEM2DAxi::elementStress(const Vec& E,
                                                       const Vec& U) const {
    std::vector<AxiQ4Element::Vec4> out(static_cast<size_t>(grid_.nElems()));
    for (int ej = 0; ej < grid_.nz(); ++ej)
        for (int ei = 0; ei < grid_.nr(); ++ei) {
            std::array<double, 4> rn, zn;
            elementCoords(ei, ej, rn, zn);
            const auto S = AxiQ4Element::stressMatrix(rn, zn, nu_);
            const auto dofs = grid_.elementDofs(ei, ej);
            AxiQ4Element::Vec8 ue;
            for (int a = 0; a < 8; ++a)
                ue(a) = U(dofs[static_cast<size_t>(a)]);
            const double Ee = E(grid_.elemId(ei, ej));
            out[static_cast<size_t>(grid_.elemId(ei, ej))] = Ee * (S * ue);
        }
    return out;
}

} // namespace topopt
