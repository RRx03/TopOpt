#include "physics/CHTSolver.hpp"

#include <array>
#include <cmath>

#include <Eigen/SparseLU>

namespace topopt {

namespace {

// Local node natural coords, order l = di + 2*dj + 4*dk, in {-1,+1}.
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

// dN/d(natural), dN[a][d], d = d/dxi, d/deta, d/dzeta.
void shapeDeriv(double xi, double eta, double zeta,
                const std::array<NodeNat, 8>& nn, double dN[8][3]) {
    for (int a = 0; a < 8; ++a) {
        const NodeNat& p = nn[static_cast<size_t>(a)];
        dN[a][0] = 0.125 * p.xi * (1 + p.eta * eta) * (1 + p.zeta * zeta);
        dN[a][1] = 0.125 * (1 + p.xi * xi) * p.eta * (1 + p.zeta * zeta);
        dN[a][2] = 0.125 * (1 + p.xi * xi) * (1 + p.eta * eta) * p.zeta;
    }
}

// Optimal SUPG streamline parameter with a stable small-Péclet limit
// (coth(Pe) − 1/Pe → Pe/3 as Pe → 0).
double supgTau(double speed, double h, double k) {
    if (speed <= 1e-14 || k <= 0.0) return 0.0;
    const double Pe = speed * h / (2.0 * k);
    const double xi = (Pe > 1e-6) ? (1.0 / std::tanh(Pe) - 1.0 / Pe) : (Pe / 3.0);
    return h / (2.0 * speed) * xi;
}

} // namespace

CHTSolver::CHTSolver(const Grid3D& grid, double h) : grid_(grid), h_(h) {}

CHTSolver::Vec CHTSolver::solve(const std::vector<double>& kElem,
                                const Vec& velocity, const Vec& Q,
                                const std::vector<std::uint8_t>& dirMask,
                                const Vec& dirVal, bool supg) const {
    const int nN = grid_.nNodes();
    const int nE = grid_.nElems();

    const auto nn = nodeNat();
    const double g = 1.0 / std::sqrt(3.0);
    const double gp[2] = {-g, g};
    // Cube cell of side h: J = (h/2) I, detJ = (h/2)^3, Jinv = (2/h) I.
    const double detJ = (h_ * 0.5) * (h_ * 0.5) * (h_ * 0.5);
    const double jinv = 2.0 / h_;

    std::vector<Eigen::Triplet<double>> trip;
    trip.reserve(static_cast<size_t>(nE) * 64);
    Vec F = Vec::Zero(nN);

    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const double ke = kElem[static_cast<size_t>(eid)];

                Eigen::Matrix<double, 8, 8> Ke =
                    Eigen::Matrix<double, 8, 8>::Zero();
                Eigen::Matrix<double, 8, 1> fe =
                    Eigen::Matrix<double, 8, 1>::Zero();

                for (int ig = 0; ig < 2; ++ig)
                    for (int jg = 0; jg < 2; ++jg)
                        for (int kg = 0; kg < 2; ++kg) {
                            double N[8], dNr[8][3];
                            shapeVal(gp[ig], gp[jg], gp[kg], nn, N);
                            shapeDeriv(gp[ig], gp[jg], gp[kg], nn, dNr);

                            // Physical gradients and interpolated velocity/source.
                            double dNdx[8][3];
                            double ug[3] = {0.0, 0.0, 0.0};
                            double Qg = 0.0;
                            for (int a = 0; a < 8; ++a) {
                                const int na = nodes[static_cast<size_t>(a)];
                                for (int i = 0; i < 3; ++i) {
                                    dNdx[a][i] = jinv * dNr[a][i];
                                    ug[i] += N[a] * velocity(3 * na + i);
                                }
                                Qg += N[a] * Q(na);
                            }
                            const double speed =
                                std::sqrt(ug[0] * ug[0] + ug[1] * ug[1] +
                                          ug[2] * ug[2]);
                            const double tau =
                                supg ? supgTau(speed, h_, ke) : 0.0;

                            double udotG[8];
                            for (int a = 0; a < 8; ++a)
                                udotG[a] = ug[0] * dNdx[a][0] +
                                           ug[1] * dNdx[a][1] +
                                           ug[2] * dNdx[a][2];

                            for (int a = 0; a < 8; ++a) {
                                for (int b = 0; b < 8; ++b) {
                                    const double kd =
                                        ke * (dNdx[a][0] * dNdx[b][0] +
                                              dNdx[a][1] * dNdx[b][1] +
                                              dNdx[a][2] * dNdx[b][2]);
                                    const double ca = N[a] * udotG[b];
                                    const double s =
                                        supg ? tau * udotG[a] * udotG[b] : 0.0;
                                    Ke(a, b) += (kd + ca + s) * detJ;
                                }
                                const double fs = supg ? tau * udotG[a] * Qg : 0.0;
                                fe(a) += (N[a] * Qg + fs) * detJ;
                            }
                        }

                for (int a = 0; a < 8; ++a) {
                    const int na = nodes[static_cast<size_t>(a)];
                    F(na) += fe(a);
                    for (int b = 0; b < 8; ++b)
                        trip.emplace_back(na, nodes[static_cast<size_t>(b)],
                                          Ke(a, b));
                }
            }

    SpMat A(nN, nN);
    A.setFromTriplets(trip.begin(), trip.end());
    A.makeCompressed();

    // Non-zero Dirichlet lift: move prescribed columns to the RHS.
    Vec lift = Vec::Zero(nN);
    for (int n = 0; n < nN; ++n)
        if (dirMask[static_cast<size_t>(n)]) lift(n) = dirVal(n);
    const Vec rhs = F - A * lift;

    std::vector<int> map(static_cast<size_t>(nN), -1);
    int nf = 0;
    for (int n = 0; n < nN; ++n)
        if (!dirMask[static_cast<size_t>(n)]) map[static_cast<size_t>(n)] = nf++;

    std::vector<Eigen::Triplet<double>> tr;
    tr.reserve(trip.size());
    for (int k = 0; k < A.outerSize(); ++k)
        for (SpMat::InnerIterator it(A, k); it; ++it) {
            const int r = map[static_cast<size_t>(it.row())];
            const int c = map[static_cast<size_t>(it.col())];
            if (r >= 0 && c >= 0) tr.emplace_back(r, c, it.value());
        }

    SpMat Ar(nf, nf);
    Ar.setFromTriplets(tr.begin(), tr.end());
    Ar.makeCompressed();

    Vec br(nf);
    for (int n = 0; n < nN; ++n)
        if (map[static_cast<size_t>(n)] >= 0) br(map[static_cast<size_t>(n)]) = rhs(n);

    Eigen::SparseLU<SpMat> solver;
    solver.compute(Ar);
    const Vec xr = solver.solve(br);

    Vec T = lift;  // Dirichlet nodes already hold dirVal; free nodes are 0.
    for (int n = 0; n < nN; ++n)
        if (map[static_cast<size_t>(n)] >= 0) T(n) = xr(map[static_cast<size_t>(n)]);
    return T;
}

} // namespace topopt
