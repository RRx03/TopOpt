#include "adjoint/ThermalObjectiveAdjoint.hpp"

#include <algorithm>
#include <cmath>

#include <Eigen/SparseLU>

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

ThermalObjectiveAdjoint::ThermalObjectiveAdjoint(
    const Grid3D& grid, const Params& prm,
    const std::vector<int>& stokesFixedDofs,
    const std::array<double, 3>& bodyForce,
    const std::vector<std::uint8_t>& thermalDirMask, const Vec& thermalDirVal,
    const Vec& Q)
    : grid_(grid), prm_(prm), bodyForce_(bodyForce), dirValT_(thermalDirVal),
      dirMaskT_(thermalDirMask), Q_(Q) {
    // Element operators (unit cube, h = 1).
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
}

double ThermalObjectiveAdjoint::alpha(double gv) const {
    const double gg = clamp01(gv);
    return prm_.alphaMax +
           (prm_.alphaMin - prm_.alphaMax) * gg * (1.0 + prm_.qBrink) /
               (gg + prm_.qBrink);
}
double ThermalObjectiveAdjoint::dAlpha(double gv) const {
    const double gg = clamp01(gv);
    const double d = gg + prm_.qBrink;
    return (prm_.alphaMin - prm_.alphaMax) * (1.0 + prm_.qBrink) * prm_.qBrink /
           (d * d);
}

// ---- Stokes-Brinkman assembly (reduced) ------------------------------------
void ThermalObjectiveAdjoint::buildStokes(const Vec& gamma, SpMat& Afree,
                                          Vec& ffree) const {
    std::vector<Eigen::Triplet<double>> tr;
    tr.reserve(static_cast<size_t>(grid_.nElems()) * 32 * 32);
    Vec fFull = Vec::Zero(4 * grid_.nNodes());
    // int N_a dV on the unit cube = 1/8 by symmetry.
    const double sLoad = 1.0 / 8.0;

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

// ---- CHT operator (full): diffusion k(g) L0 + advection C_a(u) -------------
ThermalObjectiveAdjoint::SpMat ThermalObjectiveAdjoint::buildThermalFull(
    const Vec& gamma, const Vec& vel3) const {
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

                Eigen::Matrix<double, 8, 8> Ke = ke * l0_;  // symmetric diffusion
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

ThermalObjectiveAdjoint::Vec ThermalObjectiveAdjoint::thermalSource() const {
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
                                F(nodes[static_cast<size_t>(a)]) += N[a] * Qg * detJ;
                        }
            }
    return F;
}

ThermalObjectiveAdjoint::Vec ThermalObjectiveAdjoint::velocity3(
    const Vec& w) const {
    const int nN = grid_.nNodes();
    Vec v = Vec::Zero(3 * nN);
    for (int n = 0; n < nN; ++n)
        for (int c = 0; c < 3; ++c) v(3 * n + c) = w(4 * n + c);
    return v;
}

double ThermalObjectiveAdjoint::maxSpeed(const Vec& w) const {
    const int nN = grid_.nNodes();
    double s = 0.0;
    for (int n = 0; n < nN; ++n) {
        const double ux = w(4 * n + 0), uy = w(4 * n + 1), uz = w(4 * n + 2);
        s = std::max(s, std::sqrt(ux * ux + uy * uy + uz * uz));
    }
    return s;
}

void ThermalObjectiveAdjoint::forward(const Vec& gamma, Vec& w, Vec& T) const {
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
}

// J_T = ( sum_e s_e T_e^P )^{1/P}, T_e = element-mean nodal temperature.
double ThermalObjectiveAdjoint::computeJT(const Vec& gamma, const Vec& T) const {
    double S = 0.0;
    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                double Te = 0.0;
                for (int a = 0; a < 8; ++a) Te += T(nodes[static_cast<size_t>(a)]);
                Te *= 0.125;
                const double se = 1.0 - clamp01(gamma(eid));
                S += se * std::pow(Te, prm_.P);
            }
    return std::pow(S, 1.0 / prm_.P);
}

double ThermalObjectiveAdjoint::objective(const Vec& gamma) const {
    Vec w, T;
    forward(gamma, w, T);
    return computeJT(gamma, T);
}

// -(dRt/du)^T lt, on the velocity DOFs (pressure DOFs stay zero).
// (dRt/du)^T lt at (node b, comp c) = int lt(x) N_b dT/dx_c.
ThermalObjectiveAdjoint::Vec ThermalObjectiveAdjoint::stokesAdjointRhs(
    const Vec& T, const Vec& lamT) const {
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
                                    rhs(4 * nb + c) += -lamg * N[b] * dTdx[c] * detJ;
                            }
                        }
            }
    return rhs;
}

ThermalObjectiveAdjoint::Solution ThermalObjectiveAdjoint::solve(
    const Vec& gamma) const {
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

    sol.J = computeJT(gamma, sol.T);
    const double P = prm_.P;
    // dJ_T/dS = (1/P) S^{1/P-1} = (1/P) J_T^{1-P}.  With J_T = S^{1/P}.
    const double dJdS = (1.0 / P) * std::pow(sol.J, 1.0 - P);

    // --- adjoint RHS for the thermal solve: -dJ_T/dT (full nodal), reduced.
    // dJ_T/dT_e = J_T^{1-P} s_e T_e^{P-1}, distributed 1/8 to each of 8 nodes.
    Vec dJdT = Vec::Zero(grid_.nNodes());
    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                double Te = 0.0;
                for (int a = 0; a < 8; ++a) Te += sol.T(nodes[static_cast<size_t>(a)]);
                Te *= 0.125;
                const double se = 1.0 - clamp01(gamma(eid));
                // dJ_T/dT_e = dJdS * s_e * P * T_e^{P-1}.
                const double dJdTe = dJdS * se * P * std::pow(Te, P - 1.0);
                for (int a = 0; a < 8; ++a)
                    dJdT(nodes[static_cast<size_t>(a)]) += dJdTe * 0.125;
            }

    // --- adjoint cascade (inverse order).
    // 1. K_t^T lt = -dJ_T/dT.
    const SpMat KtRT = SpMat(KtR.transpose());
    const Vec lamTr = sparseLUsolve(KtRT, reduceVec(-dJdT, tMap_, nFreeT_));
    const Vec lamT = expandVec(lamTr, tMap_, grid_.nNodes());

    // 2. A^T ls = -(dRt/du)^T lt.
    const SpMat AT = SpMat(A.transpose());
    const Vec lamSr =
        sparseLUsolve(AT, reduceVec(stokesAdjointRhs(sol.T, lamT), sMap_, nFreeS_));
    const Vec lamS = expandVec(lamSr, sMap_, 4 * grid_.nNodes());

    // --- gradient (explicit + thermal + Stokes).
    const int nEl = grid_.nElems();
    sol.termStokes = Vec::Zero(nEl);
    sol.termThermal = Vec::Zero(nEl);
    sol.termExplicit = Vec::Zero(nEl);

    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const double gv = gamma(eid);

                Eigen::Matrix<double, 8, 1> Te, lT;
                double Tmean = 0.0;
                for (int a = 0; a < 8; ++a) {
                    const int na = nodes[static_cast<size_t>(a)];
                    Te(a) = sol.T(na);
                    lT(a) = lamT(na);
                    Tmean += sol.T(na);
                }
                Tmean *= 0.125;

                // Explicit: dJ_T/dgamma|exp = dJdS * T_e^P * (ds_e/dgamma), ds/dg=-1.
                const double dsdg = (gv > 0.0 && gv < 1.0) ? -1.0 : 0.0;
                sol.termExplicit(eid) =
                    dJdS * std::pow(Tmean, P) * dsdg;

                // Thermal: dk * lt^T L0 T   (advection is gamma-independent).
                const double dk = prm_.kf - prm_.ks;
                sol.termThermal(eid) = dk * (lT.transpose() * l0_ * Te)(0, 0);

                // Stokes: dalpha * sum_c ls_u[c]^T M_vel w_u[c].
                const double da = dAlpha(clamp01(gv));
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

    sol.grad = sol.termStokes + sol.termThermal + sol.termExplicit;
    return sol;
}

} // namespace topopt
