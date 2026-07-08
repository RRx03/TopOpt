#include "adjoint/DissipationAdjoint.hpp"

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
        if (map[static_cast<size_t>(i)] >= 0)
            r(map[static_cast<size_t>(i)]) = v(i);
    return r;
}

Eigen::VectorXd expandVec(const Eigen::VectorXd& vr, const std::vector<int>& map,
                          int nFull) {
    Eigen::VectorXd v = Eigen::VectorXd::Zero(nFull);
    for (int i = 0; i < nFull; ++i)
        if (map[static_cast<size_t>(i)] >= 0)
            v(i) = vr(map[static_cast<size_t>(i)]);
    return v;
}

Eigen::VectorXd sparseLUsolve(const Eigen::SparseMatrix<double>& A,
                              const Eigen::VectorXd& b) {
    Eigen::SparseLU<Eigen::SparseMatrix<double>> lu;
    lu.compute(A);
    return lu.solve(b);
}

} // namespace

DissipationAdjoint::DissipationAdjoint(const Grid3D& grid, const Params& prm,
                                       const std::vector<int>& fixedDofs,
                                       const std::array<double, 3>& bodyForce,
                                       const Vec& dirichletVal)
    : grid_(grid), prm_(prm), bodyForce_(bodyForce), dirVal_(dirichletVal) {
    l0_ = H8Element::diffusion();
    mvel_ = H8Element::mass();

    // Constant Stokes saddle-point element matrix (viscous + pressure + PSPG),
    // built exactly as StokesSolver/TripleAdjoint (unit cube, h = 1).
    const auto nn = nodeNat();
    const double g = 1.0 / std::sqrt(3.0);
    const double gp[2] = {-g, g};
    const double detJ = 1.0 / 8.0;
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

    const int nS = 4 * grid_.nNodes();
    std::vector<char> sf(static_cast<size_t>(nS), 0);
    for (int d : fixedDofs) sf[static_cast<size_t>(d)] = 1;
    sMap_.assign(static_cast<size_t>(nS), -1);
    nFree_ = 0;
    for (int d = 0; d < nS; ++d)
        if (!sf[static_cast<size_t>(d)]) sMap_[static_cast<size_t>(d)] = nFree_++;
}

double DissipationAdjoint::alpha(double gv) const {
    const double gg = clamp01(gv);
    return prm_.alphaMax +
           (prm_.alphaMin - prm_.alphaMax) * gg * (1.0 + prm_.qBrink) /
               (gg + prm_.qBrink);
}
double DissipationAdjoint::dAlpha(double gv) const {
    const double gg = clamp01(gv);
    const double d = gg + prm_.qBrink;
    return (prm_.alphaMin - prm_.alphaMax) * (1.0 + prm_.qBrink) * prm_.qBrink /
           (d * d);
}

void DissipationAdjoint::buildStokesFull(const Vec& gamma, SpMat& Afull,
                                         Vec& fFull) const {
    const int nS = 4 * grid_.nNodes();
    std::vector<Eigen::Triplet<double>> tr;
    tr.reserve(static_cast<size_t>(grid_.nElems()) * 32 * 32);
    fFull = Vec::Zero(nS);
    const double sLoad = 1.0 / 8.0;  // ∫ N_a dV on the unit cube

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
                    const int na = a / 4, ca = a % 4;
                    for (int b = 0; b < 32; ++b) {
                        double v = stokesKe_(a, b);
                        if (ca < 3 && ca == b % 4)
                            v += aE * mvel_(na, b / 4);
                        tr.emplace_back(ldof[static_cast<size_t>(a)],
                                        ldof[static_cast<size_t>(b)], v);
                    }
                }
                for (int a = 0; a < 8; ++a)
                    for (int d = 0; d < 3; ++d)
                        fFull(4 * nodes[static_cast<size_t>(a)] + d) +=
                            bodyForce_[static_cast<size_t>(d)] * sLoad;
            }

    Afull = SpMat(nS, nS);
    Afull.setFromTriplets(tr.begin(), tr.end());
    Afull.makeCompressed();
}

// H w: velocity block (mu*L + alpha(g)*M_vel) applied per component; pressure
// rows are zero. Assembled by element accumulation (= global H w).
DissipationAdjoint::Vec DissipationAdjoint::applyH(const Vec& gamma,
                                                   const Vec& w) const {
    Vec y = Vec::Zero(4 * grid_.nNodes());
    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const double aE = alpha(gamma(eid));
                for (int c = 0; c < 3; ++c) {
                    Eigen::Matrix<double, 8, 1> uc;
                    for (int a = 0; a < 8; ++a)
                        uc(a) = w(4 * nodes[static_cast<size_t>(a)] + c);
                    const Eigen::Matrix<double, 8, 1> yc =
                        prm_.mu * (l0_ * uc) + aE * (mvel_ * uc);
                    for (int a = 0; a < 8; ++a)
                        y(4 * nodes[static_cast<size_t>(a)] + c) += yc(a);
                }
            }
    return y;
}

DissipationAdjoint::Vec DissipationAdjoint::forward(const Vec& gamma) const {
    SpMat Afull;
    Vec fFull;
    buildStokesFull(gamma, Afull, fFull);
    // Lift for nonzero Dirichlet (imposed inlet velocity): solve for the free
    // part, rhs -= A * dirVal.
    const Vec rhsFull = fFull - Afull * dirVal_;
    const SpMat Aff = reduceMat(Afull, sMap_, nFree_);
    const Vec wr = sparseLUsolve(Aff, reduceVec(rhsFull, sMap_, nFree_));
    return dirVal_ + expandVec(wr, sMap_, 4 * grid_.nNodes());
}

double DissipationAdjoint::objective(const Vec& gamma) const {
    const Vec w = forward(gamma);
    return 0.5 * w.dot(applyH(gamma, w));
}

DissipationAdjoint::Solution DissipationAdjoint::solve(const Vec& gamma) const {
    Solution sol;
    const int nS = 4 * grid_.nNodes();

    // --- forward.
    SpMat Afull;
    Vec fFull;
    buildStokesFull(gamma, Afull, fFull);
    const SpMat Aff = reduceMat(Afull, sMap_, nFree_);
    const Vec rhsFull = fFull - Afull * dirVal_;
    const Vec wr = sparseLUsolve(Aff, reduceVec(rhsFull, sMap_, nFree_));
    sol.w = dirVal_ + expandVec(wr, sMap_, nS);

    const Vec Hw = applyH(gamma, sol.w);
    sol.Phi = 0.5 * sol.w.dot(Hw);

    // --- adjoint: A^T lambda = -H w  (only free rows; lambda = 0 at fixed).
    const SpMat AffT = SpMat(Aff.transpose());
    const Vec lamR = sparseLUsolve(AffT, reduceVec(-Hw, sMap_, nFree_));
    sol.lambda = expandVec(lamR, sMap_, nS);

    // --- gradient (explicit + adjoint contributions).
    const int nEl = grid_.nElems();
    sol.termExplicit = Vec::Zero(nEl);
    sol.termAdjoint = Vec::Zero(nEl);
    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const double da = dAlpha(gamma(eid));
                double quad = 0.0;    // u^T M_vel u
                double cross = 0.0;   // lambda^T M_vel u
                for (int c = 0; c < 3; ++c) {
                    Eigen::Matrix<double, 8, 1> uc, lc;
                    for (int a = 0; a < 8; ++a) {
                        const int nd = 4 * nodes[static_cast<size_t>(a)] + c;
                        uc(a) = sol.w(nd);
                        lc(a) = sol.lambda(nd);
                    }
                    const Eigen::Matrix<double, 8, 1> Mu = mvel_ * uc;
                    quad += uc.dot(Mu);
                    cross += lc.dot(Mu);
                }
                sol.termExplicit(eid) = 0.5 * da * quad;
                sol.termAdjoint(eid) = da * cross;
            }
    sol.grad = sol.termExplicit + sol.termAdjoint;

    // --- self-adjoint coherence check: ||lambda_u + u|| / ||u||.
    double num = 0.0, den = 0.0;
    for (int n = 0; n < grid_.nNodes(); ++n)
        for (int c = 0; c < 3; ++c) {
            const double u = sol.w(4 * n + c);
            const double lu = sol.lambda(4 * n + c);
            num += (lu + u) * (lu + u);
            den += u * u;
        }
    sol.selfAdjResidual = std::sqrt(num / std::max(den, 1e-300));
    return sol;
}

} // namespace topopt
