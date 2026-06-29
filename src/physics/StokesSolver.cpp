#include "physics/StokesSolver.hpp"

#include <array>
#include <cmath>

#include <Eigen/SparseLU>

namespace topopt {

namespace {

// Local node natural coordinates, order l = di + 2*dj + 4*dk, in {-1,+1}.
struct NodeNat {
    double xi, eta, zeta;
};
std::array<NodeNat, 8> nodeNat() {
    std::array<NodeNat, 8> nn{};
    for (int dk = 0; dk < 2; ++dk)
        for (int dj = 0; dj < 2; ++dj)
            for (int di = 0; di < 2; ++di) {
                const int l = di + 2 * dj + 4 * dk;
                nn[static_cast<size_t>(l)] = {2.0 * di - 1.0, 2.0 * dj - 1.0,
                                              2.0 * dk - 1.0};
            }
    return nn;
}

void shapeVal(double xi, double eta, double zeta,
              const std::array<NodeNat, 8>& nn, double N[8]) {
    for (int a = 0; a < 8; ++a) {
        const NodeNat& p = nn[static_cast<size_t>(a)];
        N[a] = 0.125 * (1 + p.xi * xi) * (1 + p.eta * eta) * (1 + p.zeta * zeta);
    }
}

// Shape derivatives w.r.t. natural coords: dN[a][d], d = 0,1,2.
void shapeDeriv(double xi, double eta, double zeta,
                const std::array<NodeNat, 8>& nn, double dN[8][3]) {
    for (int a = 0; a < 8; ++a) {
        const NodeNat& p = nn[static_cast<size_t>(a)];
        dN[a][0] = 0.125 * p.xi * (1 + p.eta * eta) * (1 + p.zeta * zeta);
        dN[a][1] = 0.125 * (1 + p.xi * xi) * p.eta * (1 + p.zeta * zeta);
        dN[a][2] = 0.125 * (1 + p.xi * xi) * (1 + p.eta * eta) * p.zeta;
    }
}

} // namespace

StokesSolver::StokesSolver(const Grid3D& grid, double mu, double h,
                           double alphaStab)
    : grid_(grid), mu_(mu), h_(h), alphaStab_(alphaStab) {
    const auto nn = nodeNat();
    const double g = 1.0 / std::sqrt(3.0);
    const double gp[2] = {-g, g};
    const double detJ = h * h * h / 8.0;   // diagonal Jacobian, side h
    const double jinv = 2.0 / h;           // d/dx = jinv * d/dxi

    // Geometric element matrices on the cube of side h (2x2x2 Gauss, w = 1).
    Eigen::Matrix<double, 8, 8> Le = Eigen::Matrix<double, 8, 8>::Zero();
    std::array<Eigen::Matrix<double, 8, 8>, 3> Gd{};
    for (auto& m : Gd) m.setZero();
    sLoad_.fill(0.0);

    for (int ig = 0; ig < 2; ++ig)
        for (int jg = 0; jg < 2; ++jg)
            for (int kg = 0; kg < 2; ++kg) {
                double N[8], dN[8][3];
                shapeVal(gp[ig], gp[jg], gp[kg], nn, N);
                shapeDeriv(gp[ig], gp[jg], gp[kg], nn, dN);
                double dNdx[8][3];
                for (int a = 0; a < 8; ++a)
                    for (int d = 0; d < 3; ++d) dNdx[a][d] = jinv * dN[a][d];

                for (int a = 0; a < 8; ++a) {
                    sLoad_[static_cast<size_t>(a)] += N[a] * detJ;
                    for (int b = 0; b < 8; ++b) {
                        double lab = 0.0;
                        for (int d = 0; d < 3; ++d)
                            lab += dNdx[a][d] * dNdx[b][d];
                        Le(a, b) += lab * detJ;
                        for (int d = 0; d < 3; ++d)
                            Gd[static_cast<size_t>(d)](a, b) +=
                                N[a] * dNdx[b][d] * detJ;
                    }
                }
            }

    // Assemble the 32x32 element saddle-point matrix (local dof = 4a + c).
    const double tau = alphaStab_ * h_ * h_ / mu_;
    ke_.setZero();
    for (int a = 0; a < 8; ++a)
        for (int b = 0; b < 8; ++b) {
            // Viscous A: mu * scalar Laplacian, block-diagonal per velocity comp.
            for (int d = 0; d < 3; ++d)
                ke_(4 * a + d, 4 * b + d) += mu_ * Le(a, b);
            for (int d = 0; d < 3; ++d) {
                // Bᵀ : (vel a,d) <- (pres b) = −∫ N_b ∂N_a/∂x_d = −Gd(b,a)
                ke_(4 * a + d, 4 * b + 3) += -Gd[static_cast<size_t>(d)](b, a);
                // B  : (pres a) <- (vel b,d) = −∫ N_a ∂N_b/∂x_d = −Gd(a,b)
                ke_(4 * a + 3, 4 * b + d) += -Gd[static_cast<size_t>(d)](a, b);
            }
            // −C : PSPG pressure Laplacian.
            ke_(4 * a + 3, 4 * b + 3) += -tau * Le(a, b);
        }
}

void StokesSolver::setFixedDofs(const std::vector<int>& fixedDofs) {
    const int n = nDofTotal();
    std::vector<char> fixed(static_cast<size_t>(n), 0);
    for (int d : fixedDofs) fixed[static_cast<size_t>(d)] = 1;

    dofMap_.assign(static_cast<size_t>(n), -1);
    nFree_ = 0;
    for (int d = 0; d < n; ++d)
        if (!fixed[static_cast<size_t>(d)])
            dofMap_[static_cast<size_t>(d)] = nFree_++;
}

StokesSolver::Vec StokesSolver::solve(
    const std::array<double, 3>& bodyForce) const {
    return solve(bodyForce, Vec::Zero(nDofTotal()));
}

StokesSolver::Vec StokesSolver::solve(
    const std::array<double, 3>& bodyForce, const Vec& nodalLoad) const {
    const int n = nFree_;
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(static_cast<size_t>(grid_.nElems()) * 32 * 32);

    Vec Fr = Vec::Zero(n);

    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                std::array<int, 32> ldof{};
                for (int a = 0; a < 8; ++a)
                    for (int c = 0; c < 4; ++c)
                        ldof[static_cast<size_t>(4 * a + c)] =
                            4 * nodes[static_cast<size_t>(a)] + c;

                for (int a = 0; a < 32; ++a) {
                    const int ra = dofMap_[static_cast<size_t>(ldof[static_cast<size_t>(a)])];
                    if (ra < 0) continue;
                    for (int b = 0; b < 32; ++b) {
                        const int rb = dofMap_[static_cast<size_t>(ldof[static_cast<size_t>(b)])];
                        if (rb < 0) continue;
                        triplets.emplace_back(ra, rb, ke_(a, b));
                    }
                }
                // Body-force load on velocity DOFs (pressure RHS block = 0).
                for (int a = 0; a < 8; ++a)
                    for (int d = 0; d < 3; ++d) {
                        const int r = dofMap_[static_cast<size_t>(4 * nodes[static_cast<size_t>(a)] + d)];
                        if (r >= 0)
                            Fr(r) += bodyForce[static_cast<size_t>(d)] *
                                     sLoad_[static_cast<size_t>(a)];
                    }
            }

    // Explicit nodal RHS (e.g. consistent boundary-traction load).
    for (int d = 0; d < nDofTotal(); ++d) {
        const int r = dofMap_[static_cast<size_t>(d)];
        if (r >= 0) Fr(r) += nodalLoad(d);
    }

    SpMat K(n, n);
    K.setFromTriplets(triplets.begin(), triplets.end());
    K.makeCompressed();

    Eigen::SparseLU<SpMat> solver;
    solver.analyzePattern(K);
    solver.factorize(K);
    const Vec Ur = solver.solve(Fr);

    Vec U = Vec::Zero(nDofTotal());
    for (int d = 0; d < nDofTotal(); ++d) {
        const int r = dofMap_[static_cast<size_t>(d)];
        if (r >= 0) U(d) = Ur(r);
    }
    return U;
}

} // namespace topopt
