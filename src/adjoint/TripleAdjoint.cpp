#include "adjoint/TripleAdjoint.hpp"

#include <algorithm>
#include <cmath>

#include <Eigen/SparseLU>

#include "topopt/StressModel.hpp"

namespace topopt {

namespace {

inline double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }

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
void shapeDeriv(double xi, double eta, double zeta,
                const std::array<NodeNat, 8>& nn, double dN[8][3]) {
    for (int a = 0; a < 8; ++a) {
        const NodeNat& p = nn[static_cast<size_t>(a)];
        dN[a][0] = 0.125 * p.xi * (1 + p.eta * eta) * (1 + p.zeta * zeta);
        dN[a][1] = 0.125 * (1 + p.xi * xi) * p.eta * (1 + p.zeta * zeta);
        dN[a][2] = 0.125 * (1 + p.xi * xi) * (1 + p.eta * eta) * p.zeta;
    }
}

// Reduce a full sparse matrix to its free-free block via a DOF map.
Eigen::SparseMatrix<double> reduceMat(const Eigen::SparseMatrix<double>& Kfull,
                                      const std::vector<int>& map, int nFree) {
    std::vector<Eigen::Triplet<double>> tr;
    tr.reserve(static_cast<size_t>(Kfull.nonZeros()));
    for (int k = 0; k < Kfull.outerSize(); ++k)
        for (Eigen::SparseMatrix<double>::InnerIterator it(Kfull, k); it; ++it) {
            const int r = map[static_cast<size_t>(it.row())];
            const int c = map[static_cast<size_t>(it.col())];
            if (r >= 0 && c >= 0) tr.emplace_back(r, c, it.value());
        }
    Eigen::SparseMatrix<double> Kr(nFree, nFree);
    Kr.setFromTriplets(tr.begin(), tr.end());
    Kr.makeCompressed();
    return Kr;
}

Eigen::VectorXd reduceVec(const Eigen::VectorXd& v, const std::vector<int>& map,
                          int nFree) {
    Eigen::VectorXd r(nFree);
    for (int i = 0; i < static_cast<int>(map.size()); ++i)
        if (map[static_cast<size_t>(i)] >= 0) r(map[static_cast<size_t>(i)]) = v(i);
    return r;
}

Eigen::VectorXd expandVec(const Eigen::VectorXd& vr, const std::vector<int>& map,
                          int nFull) {
    Eigen::VectorXd v = Eigen::VectorXd::Zero(nFull);
    for (int i = 0; i < nFull; ++i)
        if (map[static_cast<size_t>(i)] >= 0) v(i) = vr(map[static_cast<size_t>(i)]);
    return v;
}

Eigen::VectorXd sparseLUsolve(const Eigen::SparseMatrix<double>& A,
                              const Eigen::VectorXd& b) {
    Eigen::SparseLU<Eigen::SparseMatrix<double>> lu;
    lu.compute(A);
    return lu.solve(b);
}

} // namespace

TripleAdjoint::TripleAdjoint(const Grid3D& grid, const Params& prm,
                             const std::vector<int>& stokesFixedDofs,
                             const std::array<double, 3>& bodyForce,
                             const std::vector<std::uint8_t>& thermalDirMask,
                             const Vec& thermalDirVal, const Vec& Q,
                             const std::vector<int>& elasticFixedDofs,
                             const Vec& Fmech)
    : grid_(grid), prm_(prm), bodyForce_(bodyForce), dirValT_(thermalDirVal),
      dirMaskT_(thermalDirMask), Q_(Q), Fmech_(Fmech) {
    // Element operators (unit cube, h = 1).
    ke0_ = H8Element::stiffness(prm_.nu);
    cth_ = H8Element::thermalCoupling(prm_.nu);
    l0_ = H8Element::diffusion();
    mvel_ = H8Element::mass();

    // Constant Stokes saddle-point element matrix. Le == l0_ (same Laplacian);
    // we still need the mixed gradient blocks Gd(a,b) = int N_a dN_b/dx_d.
    const auto nn = nodeNat();
    const double g = 1.0 / std::sqrt(3.0);
    const double gp[2] = {-g, g};
    const double detJ = 1.0 / 8.0;   // unit cube
    const double jinv = 2.0;
    std::array<Eigen::Matrix<double, 8, 8>, 3> Gd{};
    for (auto& m : Gd) m.setZero();
    for (int ig = 0; ig < 2; ++ig)
        for (int jg = 0; jg < 2; ++jg)
            for (int kg = 0; kg < 2; ++kg) {
                double N[8], dNr[8][3];
                shapeVal(gp[ig], gp[jg], gp[kg], nn, N);
                shapeDeriv(gp[ig], gp[jg], gp[kg], nn, dNr);
                double dNdx[8][3];
                for (int a = 0; a < 8; ++a)
                    for (int d = 0; d < 3; ++d) dNdx[a][d] = jinv * dNr[a][d];
                for (int a = 0; a < 8; ++a)
                    for (int b = 0; b < 8; ++b)
                        for (int d = 0; d < 3; ++d)
                            Gd[static_cast<size_t>(d)](a, b) +=
                                N[a] * dNdx[b][d] * detJ;
            }

    const double tau = prm_.alphaStab * 1.0 * 1.0 / prm_.mu;  // h = 1
    stokesKe_.setZero();
    for (int a = 0; a < 8; ++a)
        for (int b = 0; b < 8; ++b) {
            for (int d = 0; d < 3; ++d)
                stokesKe_(4 * a + d, 4 * b + d) += prm_.mu * l0_(a, b);
            for (int d = 0; d < 3; ++d) {
                stokesKe_(4 * a + d, 4 * b + 3) += -Gd[static_cast<size_t>(d)](b, a);
                stokesKe_(4 * a + 3, 4 * b + d) += -Gd[static_cast<size_t>(d)](a, b);
            }
            stokesKe_(4 * a + 3, 4 * b + 3) += -tau * l0_(a, b);
        }

    // DOF maps.
    const int nS = 4 * grid_.nNodes();
    std::vector<char> sf(static_cast<size_t>(nS), 0);
    for (int d : stokesFixedDofs) sf[static_cast<size_t>(d)] = 1;
    sMap_.assign(static_cast<size_t>(nS), -1);
    nFreeS_ = 0;
    for (int d = 0; d < nS; ++d)
        if (!sf[static_cast<size_t>(d)]) sMap_[static_cast<size_t>(d)] = nFreeS_++;

    const int nN = grid_.nNodes();
    tMap_.assign(static_cast<size_t>(nN), -1);
    nFreeT_ = 0;
    for (int n = 0; n < nN; ++n)
        if (!dirMaskT_[static_cast<size_t>(n)]) tMap_[static_cast<size_t>(n)] = nFreeT_++;

    const int nE = grid_.nDof();
    std::vector<char> ef(static_cast<size_t>(nE), 0);
    for (int d : elasticFixedDofs) ef[static_cast<size_t>(d)] = 1;
    eMap_.assign(static_cast<size_t>(nE), -1);
    nFreeE_ = 0;
    for (int d = 0; d < nE; ++d)
        if (!ef[static_cast<size_t>(d)]) eMap_[static_cast<size_t>(d)] = nFreeE_++;
}

double TripleAdjoint::alpha(double gv) const {
    const double gg = clamp01(gv);
    return prm_.alphaMax +
           (prm_.alphaMin - prm_.alphaMax) * gg * (1.0 + prm_.qBrink) /
               (gg + prm_.qBrink);
}
double TripleAdjoint::dAlpha(double gv) const {
    const double gg = clamp01(gv);
    const double d = gg + prm_.qBrink;
    return (prm_.alphaMin - prm_.alphaMax) * (1.0 + prm_.qBrink) * prm_.qBrink /
           (d * d);
}
double TripleAdjoint::youngE(double gv) const {
    const double gg = clamp01(gv);
    return prm_.Emin + (prm_.E0 - prm_.Emin) * std::pow(1.0 - gg, prm_.p);
}
double TripleAdjoint::dYoungE(double gv) const {
    const double gg = clamp01(gv);
    return -prm_.p * (prm_.E0 - prm_.Emin) * std::pow(1.0 - gg, prm_.p - 1.0);
}

// ---- Stokes-Brinkman assembly (reduced) ------------------------------------
void TripleAdjoint::buildStokes(const Vec& gamma, SpMat& Afree,
                                Vec& ffree) const {
    std::vector<Eigen::Triplet<double>> tr;
    tr.reserve(static_cast<size_t>(grid_.nElems()) * 32 * 32);
    Vec fFull = Vec::Zero(4 * grid_.nNodes());
    const double detJ = 1.0 / 8.0;
    // int N_a dV on the unit cube = detJ * sum_gauss N_a. By symmetry = 1/8.
    const double sLoad = 1.0 / 8.0;
    (void)detJ;

    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const double aE = alpha(gamma(eid));
                std::array<int, 32> ldof{};
                for (int a = 0; a < 8; ++a)
                    for (int c = 0; c < 4; ++c)
                        ldof[static_cast<size_t>(4 * a + c)] =
                            4 * nodes[static_cast<size_t>(a)] + c;
                for (int a = 0; a < 32; ++a) {
                    const int ra = sMap_[static_cast<size_t>(ldof[static_cast<size_t>(a)])];
                    if (ra < 0) continue;
                    const int na = a / 4, ca = a % 4;
                    for (int b = 0; b < 32; ++b) {
                        const int rb = sMap_[static_cast<size_t>(ldof[static_cast<size_t>(b)])];
                        if (rb < 0) continue;
                        double v = stokesKe_(a, b);
                        if (ca < 3 && ca == b % 4)
                            v += aE * mvel_(na, b / 4);
                        tr.emplace_back(ra, rb, v);
                    }
                }
                for (int a = 0; a < 8; ++a)
                    for (int d = 0; d < 3; ++d)
                        fFull(4 * nodes[static_cast<size_t>(a)] + d) +=
                            bodyForce_[static_cast<size_t>(d)] * sLoad;
            }

    Afree = SpMat(nFreeS_, nFreeS_);
    Afree.setFromTriplets(tr.begin(), tr.end());
    Afree.makeCompressed();
    ffree = reduceVec(fFull, sMap_, nFreeS_);
}

// ---- CHT operator (full, unreduced): diffusion k(g) L0 + advection C_a(u) ---
TripleAdjoint::SpMat TripleAdjoint::buildThermalFull(const Vec& gamma,
                                                     const Vec& vel3) const {
    const int nN = grid_.nNodes();
    const auto nn = nodeNat();
    const double g = 1.0 / std::sqrt(3.0);
    const double gp[2] = {-g, g};
    const double detJ = 1.0 / 8.0;
    const double jinv = 2.0;

    std::vector<Eigen::Triplet<double>> tr;
    tr.reserve(static_cast<size_t>(grid_.nElems()) * 64);

    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const double ke = prm_.ks + (prm_.kf - prm_.ks) * clamp01(gamma(eid));

                Eigen::Matrix<double, 8, 8> Ke =
                    ke * l0_;  // symmetric diffusion
                // Advection C_a(a,b) = int N_a (u . grad N_b).
                for (int ig = 0; ig < 2; ++ig)
                    for (int jg = 0; jg < 2; ++jg)
                        for (int kg = 0; kg < 2; ++kg) {
                            double N[8], dNr[8][3];
                            shapeVal(gp[ig], gp[jg], gp[kg], nn, N);
                            shapeDeriv(gp[ig], gp[jg], gp[kg], nn, dNr);
                            double dNdx[8][3];
                            double ug[3] = {0, 0, 0};
                            for (int a = 0; a < 8; ++a) {
                                const int na = nodes[static_cast<size_t>(a)];
                                for (int i = 0; i < 3; ++i) {
                                    dNdx[a][i] = jinv * dNr[a][i];
                                    ug[i] += N[a] * vel3(3 * na + i);
                                }
                            }
                            double udotG[8];
                            for (int b = 0; b < 8; ++b)
                                udotG[b] = ug[0] * dNdx[b][0] + ug[1] * dNdx[b][1] +
                                           ug[2] * dNdx[b][2];
                            for (int a = 0; a < 8; ++a)
                                for (int b = 0; b < 8; ++b)
                                    Ke(a, b) += N[a] * udotG[b] * detJ;
                        }

                for (int a = 0; a < 8; ++a)
                    for (int b = 0; b < 8; ++b)
                        tr.emplace_back(nodes[static_cast<size_t>(a)],
                                        nodes[static_cast<size_t>(b)], Ke(a, b));
            }

    SpMat K(nN, nN);
    K.setFromTriplets(tr.begin(), tr.end());
    K.makeCompressed();
    return K;
}

TripleAdjoint::Vec TripleAdjoint::thermalSource() const {
    const int nN = grid_.nNodes();
    const auto nn = nodeNat();
    const double g = 1.0 / std::sqrt(3.0);
    const double gp[2] = {-g, g};
    const double detJ = 1.0 / 8.0;
    Vec F = Vec::Zero(nN);
    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                for (int ig = 0; ig < 2; ++ig)
                    for (int jg = 0; jg < 2; ++jg)
                        for (int kg = 0; kg < 2; ++kg) {
                            double N[8];
                            shapeVal(gp[ig], gp[jg], gp[kg], nn, N);
                            double Qg = 0.0;
                            for (int a = 0; a < 8; ++a)
                                Qg += N[a] * Q_(nodes[static_cast<size_t>(a)]);
                            for (int a = 0; a < 8; ++a)
                                F(nodes[static_cast<size_t>(a)]) +=
                                    N[a] * Qg * detJ;
                        }
            }
    return F;
}

// ---- Elastic operator (full) K_e = sum E_e(g) KE0 ---------------------------
TripleAdjoint::SpMat TripleAdjoint::buildElasticFull(const Vec& gamma) const {
    const int nD = grid_.nDof();
    std::vector<Eigen::Triplet<double>> tr;
    tr.reserve(static_cast<size_t>(grid_.nElems()) * 24 * 24);
    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const double Ee = youngE(gamma(grid_.elemId(ex, ey, ez)));
                const auto dofs = grid_.elementDofs(ex, ey, ez);
                for (int a = 0; a < 24; ++a)
                    for (int b = 0; b < 24; ++b)
                        tr.emplace_back(dofs[static_cast<size_t>(a)],
                                        dofs[static_cast<size_t>(b)],
                                        Ee * ke0_(a, b));
            }
    SpMat K(nD, nD);
    K.setFromTriplets(tr.begin(), tr.end());
    K.makeCompressed();
    return K;
}

TripleAdjoint::Vec TripleAdjoint::thermalLoad(const Vec& gamma,
                                              const Vec& T) const {
    Vec F = Vec::Zero(grid_.nDof());
    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const auto dofs = grid_.elementDofs(ex, ey, ez);
                Eigen::Matrix<double, 8, 1> dT;
                for (int a = 0; a < 8; ++a)
                    dT(a) = T(nodes[static_cast<size_t>(a)]) - prm_.Tref;
                const double scale =
                    youngE(gamma(grid_.elemId(ex, ey, ez))) * prm_.alphaTh;
                const Eigen::Matrix<double, 24, 1> fe = scale * (cth_ * dT);
                for (int a = 0; a < 24; ++a)
                    F(dofs[static_cast<size_t>(a)]) += fe(a);
            }
    return F;
}

TripleAdjoint::Vec TripleAdjoint::velocity3(const Vec& w) const {
    const int nN = grid_.nNodes();
    Vec v = Vec::Zero(3 * nN);
    for (int n = 0; n < nN; ++n)
        for (int c = 0; c < 3; ++c) v(3 * n + c) = w(4 * n + c);
    return v;
}

double TripleAdjoint::maxSpeed(const Vec& w) const {
    const int nN = grid_.nNodes();
    double s = 0.0;
    for (int n = 0; n < nN; ++n) {
        const double ux = w(4 * n + 0), uy = w(4 * n + 1), uz = w(4 * n + 2);
        s = std::max(s, std::sqrt(ux * ux + uy * uy + uz * uz));
    }
    return s;
}

void TripleAdjoint::forward(const Vec& gamma, Vec& w, Vec& T, Vec& U) const {
    // 1. Stokes-Brinkman.
    SpMat A;
    Vec f;
    buildStokes(gamma, A, f);
    const Vec wr = sparseLUsolve(A, f);
    w = expandVec(wr, sMap_, 4 * grid_.nNodes());

    // 2. CHT.
    const Vec vel3 = velocity3(w);
    const SpMat Kt = buildThermalFull(gamma, vel3);
    Vec lift = Vec::Zero(grid_.nNodes());
    for (int n = 0; n < grid_.nNodes(); ++n)
        if (dirMaskT_[static_cast<size_t>(n)]) lift(n) = dirValT_(n);
    const Vec rhsFull = thermalSource() - Kt * lift;
    const SpMat KtR = reduceMat(Kt, tMap_, nFreeT_);
    const Vec Tr = sparseLUsolve(KtR, reduceVec(rhsFull, tMap_, nFreeT_));
    T = lift;
    for (int n = 0; n < grid_.nNodes(); ++n)
        if (tMap_[static_cast<size_t>(n)] >= 0) T(n) = Tr(tMap_[static_cast<size_t>(n)]);

    // 3. Thermo-elastic.
    const SpMat Ke = buildElasticFull(gamma);
    const Vec rhsE = Fmech_ + thermalLoad(gamma, T);
    const SpMat KeR = reduceMat(Ke, eMap_, nFreeE_);
    const Vec Ur = sparseLUsolve(KeR, reduceVec(rhsE, eMap_, nFreeE_));
    U = expandVec(Ur, eMap_, grid_.nDof());
}

double TripleAdjoint::objective(const Vec& gamma) const {
    Vec w, T, U;
    forward(gamma, w, T, U);
    return Fmech_.dot(U);
}

// G^T le, G = dF_th/dT ; per element E_e alpha_th Cth^T le_local, by node.
TripleAdjoint::Vec TripleAdjoint::thermalAdjointRhs(const Vec& gamma,
                                                    const Vec& lamE) const {
    Vec gT = Vec::Zero(grid_.nNodes());
    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const auto dofs = grid_.elementDofs(ex, ey, ez);
                Eigen::Matrix<double, 24, 1> le;
                for (int a = 0; a < 24; ++a)
                    le(a) = lamE(dofs[static_cast<size_t>(a)]);
                const double scale =
                    youngE(gamma(grid_.elemId(ex, ey, ez))) * prm_.alphaTh;
                const Eigen::Matrix<double, 8, 1> ce =
                    scale * (cth_.transpose() * le);
                for (int a = 0; a < 8; ++a)
                    gT(nodes[static_cast<size_t>(a)]) += ce(a);
            }
    return gT;
}

// -(dRt/du)^T lt, on the velocity DOFs (pressure DOFs stay zero).
// (dRt/du)^T lt at (node b, comp c) = int lt(x) N_b dT/dx_c.
TripleAdjoint::Vec TripleAdjoint::stokesAdjointRhs(const Vec& T,
                                                   const Vec& lamT) const {
    const auto nn = nodeNat();
    const double g = 1.0 / std::sqrt(3.0);
    const double gp[2] = {-g, g};
    const double detJ = 1.0 / 8.0;
    const double jinv = 2.0;
    Vec rhs = Vec::Zero(4 * grid_.nNodes());

    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                for (int ig = 0; ig < 2; ++ig)
                    for (int jg = 0; jg < 2; ++jg)
                        for (int kg = 0; kg < 2; ++kg) {
                            double N[8], dNr[8][3];
                            shapeVal(gp[ig], gp[jg], gp[kg], nn, N);
                            shapeDeriv(gp[ig], gp[jg], gp[kg], nn, dNr);
                            double dTdx[3] = {0, 0, 0};
                            double lamg = 0.0;
                            for (int a = 0; a < 8; ++a) {
                                const int na = nodes[static_cast<size_t>(a)];
                                lamg += N[a] * lamT(na);
                                for (int i = 0; i < 3; ++i)
                                    dTdx[i] += jinv * dNr[a][i] * T(na);
                            }
                            for (int b = 0; b < 8; ++b) {
                                const int nb = nodes[static_cast<size_t>(b)];
                                for (int c = 0; c < 3; ++c)
                                    rhs(4 * nb + c) +=
                                        -lamg * N[b] * dTdx[c] * detJ;
                            }
                        }
            }
    return rhs;
}

TripleAdjoint::Solution TripleAdjoint::solve(const Vec& gamma) const {
    Solution sol;

    // --- forward cascade (kept operators reused for the transposed adjoints).
    SpMat A;
    Vec f;
    buildStokes(gamma, A, f);
    const Vec wr = sparseLUsolve(A, f);
    sol.w = expandVec(wr, sMap_, 4 * grid_.nNodes());

    const Vec vel3 = velocity3(sol.w);
    const SpMat Kt = buildThermalFull(gamma, vel3);
    Vec lift = Vec::Zero(grid_.nNodes());
    for (int n = 0; n < grid_.nNodes(); ++n)
        if (dirMaskT_[static_cast<size_t>(n)]) lift(n) = dirValT_(n);
    const SpMat KtR = reduceMat(Kt, tMap_, nFreeT_);
    const Vec rhsT = reduceVec(thermalSource() - Kt * lift, tMap_, nFreeT_);
    const Vec Tr = sparseLUsolve(KtR, rhsT);
    sol.T = lift;
    for (int n = 0; n < grid_.nNodes(); ++n)
        if (tMap_[static_cast<size_t>(n)] >= 0)
            sol.T(n) = Tr(tMap_[static_cast<size_t>(n)]);

    const SpMat Ke = buildElasticFull(gamma);
    const SpMat KeR = reduceMat(Ke, eMap_, nFreeE_);
    const Vec rhsE = reduceVec(Fmech_ + thermalLoad(gamma, sol.T), eMap_, nFreeE_);
    const Vec Ur = sparseLUsolve(KeR, rhsE);
    sol.U = expandVec(Ur, eMap_, grid_.nDof());
    sol.J = Fmech_.dot(sol.U);

    // --- adjoint cascade (inverse order).
    // 1. K_e^T le = -L  (K_e SPD -> same operator).
    const Vec lamEr = sparseLUsolve(KeR, reduceVec(-Fmech_, eMap_, nFreeE_));
    const Vec lamE = expandVec(lamEr, eMap_, grid_.nDof());

    // 2. K_t^T lt = G^T le.
    const SpMat KtRT = SpMat(KtR.transpose());
    const Vec lamTr =
        sparseLUsolve(KtRT, reduceVec(thermalAdjointRhs(gamma, lamE), tMap_, nFreeT_));
    const Vec lamT = expandVec(lamTr, tMap_, grid_.nNodes());

    // 3. A^T ls = -(dRt/du)^T lt.
    const SpMat AT = SpMat(A.transpose());
    const Vec lamSr =
        sparseLUsolve(AT, reduceVec(stokesAdjointRhs(sol.T, lamT), sMap_, nFreeS_));
    const Vec lamS = expandVec(lamSr, sMap_, 4 * grid_.nNodes());

    // --- gradient (three coupled contributions).
    const int nEl = grid_.nElems();
    sol.termStokes = Vec::Zero(nEl);
    sol.termThermal = Vec::Zero(nEl);
    sol.termElastic = Vec::Zero(nEl);

    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const auto dofs = grid_.elementDofs(ex, ey, ez);
                const double gv = clamp01(gamma(eid));

                // Elastic: dE [ le^T KE0 U - alpha_th le^T Cth (T - Tref) ].
                Eigen::Matrix<double, 24, 1> Ue, lE;
                for (int a = 0; a < 24; ++a) {
                    Ue(a) = sol.U(dofs[static_cast<size_t>(a)]);
                    lE(a) = lamE(dofs[static_cast<size_t>(a)]);
                }
                Eigen::Matrix<double, 8, 1> Te, lT, dTe;
                for (int a = 0; a < 8; ++a) {
                    const int na = nodes[static_cast<size_t>(a)];
                    Te(a) = sol.T(na);
                    lT(a) = lamT(na);
                    dTe(a) = sol.T(na) - prm_.Tref;
                }
                const double dE = dYoungE(gv);
                const double te =
                    dE * ((lE.transpose() * ke0_ * Ue)(0, 0) -
                          prm_.alphaTh * (lE.transpose() * (cth_ * dTe))(0, 0));
                sol.termElastic(eid) = te;

                // Thermal: dk * lt^T L0 T   (advection is gamma-independent).
                const double dk = prm_.kf - prm_.ks;
                sol.termThermal(eid) = dk * (lT.transpose() * l0_ * Te)(0, 0);

                // Stokes: dalpha * sum_c ls_u[c]^T M_vel w_u[c].
                const double da = dAlpha(gv);
                double ts = 0.0;
                for (int c = 0; c < 3; ++c) {
                    Eigen::Matrix<double, 8, 1> wc, lc;
                    for (int a = 0; a < 8; ++a) {
                        const int nd = 4 * nodes[static_cast<size_t>(a)] + c;
                        wc(a) = sol.w(nd);
                        lc(a) = lamS(nd);
                    }
                    ts += (lc.transpose() * mvel_ * wc)(0, 0);
                }
                sol.termStokes(eid) = da * ts;
            }

    sol.grad = sol.termStokes + sol.termThermal + sol.termElastic;
    return sol;
}

// ---- von Mises p-norm objective through the triple cascade (GATE A2) --------
// Solidity s = 1 - gamma; sigma_e = s_e^q * vm0_e with vm0 the UNIT-MODULUS
// (E = 1) solid centroid von Mises. Documented choice: sigma0 does not carry
// E(gamma), so the only explicit gamma dependence is the s^q relaxation; the
// E(gamma) dependence of the ACTUAL stress state is captured implicitly through
// U (elastic hereditary term of the adjoint).
double TripleAdjoint::stressObjective(const Vec& gamma,
                                      const StressParams& sp) const {
    Vec w, T, U;
    forward(gamma, w, T, U);
    const StressModel sm(prm_.nu, sp.q, sp.P);
    const Vec vm0 = sm.vonMisesSolid(grid_, U);
    const Vec solidity = (1.0 - gamma.array()).matrix();
    return sm.pNorm(sm.relaxedStress(solidity, vm0));
}

TripleAdjoint::StressSolution
TripleAdjoint::solveStress(const Vec& gamma, const StressParams& sp) const {
    StressSolution sol;

    // --- forward cascade, operators kept for the transposed adjoints (same
    // bricks and same order as solve(); solve() itself is left untouched).
    SpMat A;
    Vec f;
    buildStokes(gamma, A, f);
    const Vec wr = sparseLUsolve(A, f);
    sol.w = expandVec(wr, sMap_, 4 * grid_.nNodes());

    const Vec vel3 = velocity3(sol.w);
    const SpMat Kt = buildThermalFull(gamma, vel3);
    Vec lift = Vec::Zero(grid_.nNodes());
    for (int n = 0; n < grid_.nNodes(); ++n)
        if (dirMaskT_[static_cast<size_t>(n)]) lift(n) = dirValT_(n);
    const SpMat KtR = reduceMat(Kt, tMap_, nFreeT_);
    const Vec rhsT = reduceVec(thermalSource() - Kt * lift, tMap_, nFreeT_);
    const Vec Tr = sparseLUsolve(KtR, rhsT);
    sol.T = lift;
    for (int n = 0; n < grid_.nNodes(); ++n)
        if (tMap_[static_cast<size_t>(n)] >= 0)
            sol.T(n) = Tr(tMap_[static_cast<size_t>(n)]);

    const SpMat Ke = buildElasticFull(gamma);
    const SpMat KeR = reduceMat(Ke, eMap_, nFreeE_);
    const Vec rhsE = reduceVec(Fmech_ + thermalLoad(gamma, sol.T), eMap_, nFreeE_);
    const Vec Ur = sparseLUsolve(KeR, rhsE);
    sol.U = expandVec(Ur, eMap_, grid_.nDof());

    // --- objective, explicit term and adjoint seed dJ/dU ---------------------
    const StressModel sm(prm_.nu, sp.q, sp.P);
    const auto& S0 = sm.S0();
    const H8Element::Mat6 Vsym =
        0.5 * (sm.V() + H8Element::Mat6(sm.V().transpose()));

    sol.vm0 = sm.vonMisesSolid(grid_, sol.U);
    const Vec solidity = (1.0 - gamma.array()).matrix();
    const Vec sigma = sm.relaxedStress(solidity, sol.vm0);
    const double sigPN = sm.pNorm(sigma);
    sol.J = sigPN;

    const int nEl = grid_.nElems();
    const double vmFloor = 1e-12;  // guard vm0 ~ 0 in dvm0/du = (1/vm0) S0^T V s

    // dJ/dsigma_e = sigPN^(1-P) sigma_e^(P-1).
    Vec dJdsig(nEl);
    for (int e = 0; e < nEl; ++e)
        dJdsig(e) = std::pow(sigPN, 1.0 - sp.P) * std::pow(sigma(e), sp.P - 1.0);

    // dJ/dU (scattered) and explicit relaxation term
    //   dsigma_e/dg = d(s^q)/dg * vm0 = -q s^(q-1) vm0   (ds/dg = -1).
    Vec dJdU = Vec::Zero(grid_.nDof());
    sol.termExplicit = Vec::Zero(nEl);
    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto dofs = grid_.elementDofs(ex, ey, ez);
                Eigen::Matrix<double, 24, 1> ue;
                for (int a = 0; a < 24; ++a)
                    ue(a) = sol.U(dofs[static_cast<size_t>(a)]);

                const double s = std::max(solidity(eid), 0.0);
                const double sq = std::pow(s, sp.q);

                if (sol.vm0(eid) > vmFloor) {
                    const Eigen::Matrix<double, 6, 1> sv = S0 * ue;
                    const Eigen::Matrix<double, 24, 1> dsig_du =
                        (sq / sol.vm0(eid)) * (S0.transpose() * (Vsym * sv));
                    const double wgt = dJdsig(eid);
                    for (int a = 0; a < 24; ++a)
                        dJdU(dofs[static_cast<size_t>(a)]) += wgt * dsig_du(a);
                }

                // s^(q-1) diverges for q<1 at s=0: floor s (LL-008).
                const double sEps = std::max(s, 1e-9);
                sol.termExplicit(eid) = -dJdsig(eid) * sp.q *
                                        std::pow(sEps, sp.q - 1.0) *
                                        sol.vm0(eid);
            }

    // --- adjoint cascade: STRUCTURE of solve(), seed = -dJ_sigma/dU ----------
    // 1. K_e^T le = -dJ/dU  (K_e SPD -> same operator).
    const Vec lamEr = sparseLUsolve(KeR, reduceVec(-dJdU, eMap_, nFreeE_));
    const Vec lamE = expandVec(lamEr, eMap_, grid_.nDof());

    // 2. K_t^T lt = G^T le.
    const SpMat KtRT = SpMat(KtR.transpose());
    const Vec lamTr = sparseLUsolve(
        KtRT, reduceVec(thermalAdjointRhs(gamma, lamE), tMap_, nFreeT_));
    const Vec lamT = expandVec(lamTr, tMap_, grid_.nNodes());

    // 3. A^T ls = -(dRt/du)^T lt.
    const SpMat AT = SpMat(A.transpose());
    const Vec lamSr = sparseLUsolve(
        AT, reduceVec(stokesAdjointRhs(sol.T, lamT), sMap_, nFreeS_));
    const Vec lamS = expandVec(lamSr, sMap_, 4 * grid_.nNodes());

    // --- hereditary gradient terms (identical structure to solve()) ----------
    sol.termStokes = Vec::Zero(nEl);
    sol.termThermal = Vec::Zero(nEl);
    sol.termElastic = Vec::Zero(nEl);

    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const auto dofs = grid_.elementDofs(ex, ey, ez);
                const double gv = clamp01(gamma(eid));

                // Elastic: dE [ le^T KE0 U - alpha_th le^T Cth (T - Tref) ].
                Eigen::Matrix<double, 24, 1> Ue, lE;
                for (int a = 0; a < 24; ++a) {
                    Ue(a) = sol.U(dofs[static_cast<size_t>(a)]);
                    lE(a) = lamE(dofs[static_cast<size_t>(a)]);
                }
                Eigen::Matrix<double, 8, 1> Te, lT, dTe;
                for (int a = 0; a < 8; ++a) {
                    const int na = nodes[static_cast<size_t>(a)];
                    Te(a) = sol.T(na);
                    lT(a) = lamT(na);
                    dTe(a) = sol.T(na) - prm_.Tref;
                }
                const double dE = dYoungE(gv);
                sol.termElastic(eid) =
                    dE * ((lE.transpose() * ke0_ * Ue)(0, 0) -
                          prm_.alphaTh * (lE.transpose() * (cth_ * dTe))(0, 0));

                // Thermal: dk * lt^T L0 T   (advection is gamma-independent).
                const double dk = prm_.kf - prm_.ks;
                sol.termThermal(eid) = dk * (lT.transpose() * l0_ * Te)(0, 0);

                // Stokes: dalpha * sum_c ls_u[c]^T M_vel w_u[c].
                const double da = dAlpha(gv);
                double ts = 0.0;
                for (int c = 0; c < 3; ++c) {
                    Eigen::Matrix<double, 8, 1> wc, lc;
                    for (int a = 0; a < 8; ++a) {
                        const int nd = 4 * nodes[static_cast<size_t>(a)] + c;
                        wc(a) = sol.w(nd);
                        lc(a) = lamS(nd);
                    }
                    ts += (lc.transpose() * mvel_ * wc)(0, 0);
                }
                sol.termStokes(eid) = da * ts;
            }

    sol.grad = sol.termExplicit + sol.termStokes + sol.termThermal +
               sol.termElastic;
    return sol;
}

} // namespace topopt
