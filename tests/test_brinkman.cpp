// CPU Brinkman-penalization validation (no Metal), double precision.
//
// Extends the Q1-Q1 + PSPG Stokes solver with the momentum reaction α(γ)u and
// checks it against two oracles:
//
//  ORACLE 1 — Darcy-Brinkman 1D. Plane channel, UNIFORM α (γ=0 everywhere via
//    α_max), body force G, no-slip at x=0,x=Lx. The 1D momentum balance
//    −μ u'' + α u = G with u(0)=u(Lx)=0 has the closed form
//        u(x) = (G/α) [ 1 − cosh(κ(x−Lx/2)) / cosh(κ Lx/2) ],  κ = √(α/μ).
//    We pick α so that κ Lx ≈ 4 (a boundary-layer profile clearly distinct from
//    the Poiseuille parabola). PASS if max|u_FEM−u|/u_max < 2 % at nx=20 and the
//    error converges ~O(h²) (nx=10 -> nx=20). The α→0 limit (γ=1, α=α_min≈0)
//    must reproduce the Stokes Poiseuille parabola.
//
//  ORACLE 2 — non-leak (LL-LIT-004). A solid slab (γ=0 -> α=α_max) occupies the
//    lower-x half of the channel; the upper-x half stays fluid (γ=1 -> α≈0). The
//    flow is body-force driven. The mean velocity deep inside the solid, divided
//    by the fluid bulk velocity, must be < 1 % for α_max large enough. Lowering
//    α_max (e.g. 1e1) makes the leak blow up — the calibration warning.
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Grid3D.hpp"
#include "physics/StokesSolver.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

namespace {

constexpr double kMu = 1.0;
constexpr double kLx = 1.0;
constexpr double kG = 8.0;                 // body force f_z
constexpr double kAlphaStab = 1.0 / 12.0;  // PSPG coefficient (as in test_stokes)

// ---- ORACLE 1 : Darcy-Brinkman 1D ------------------------------------------

double darcyBrinkman(double x, double alpha) {
    const double kappa = std::sqrt(alpha / kMu);
    const double c = std::cosh(kappa * kLx / 2.0);
    return (kG / alpha) * (1.0 - std::cosh(kappa * (x - kLx / 2.0)) / c);
}
double poiseuille(double x) { return (kG / (2.0 * kMu)) * x * (kLx - x); }

struct DbResult {
    double relErr;   // max |u_z_fem − u_analytic| / u_max
    double uMax;
    double maxAbsUxy;
    bool solveOk;
};

// nelx x 2 x 2 channel, uniform Brinkman (γ ≡ gammaUniform), body force G.
// If useBrinkman is false the solver runs in pure-Stokes mode (no α term).
DbResult runDarcyBrinkman(int nelx, double alphaMax, double gammaUniform,
                          bool useBrinkman) {
    const int nely = 2, nelz = 2;
    const double h = kLx / nelx;
    Grid3D g(nelx, nely, nelz);
    StokesSolver stokes(g, kMu, h, kAlphaStab);
    stokes.setBrinkman(alphaMax, 0.0, 0.1);

    std::vector<int> fixed;
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            for (int i = 0; i <= nelx; ++i) {
                const int n = g.nodeId(i, j, k);
                if (i == 0 || i == nelx) {          // no-slip walls
                    fixed.push_back(StokesSolver::dof(n, 0));
                    fixed.push_back(StokesSolver::dof(n, 1));
                    fixed.push_back(StokesSolver::dof(n, 2));
                } else if (j == 0 || j == nely) {   // y symmetry / slip
                    fixed.push_back(StokesSolver::dof(n, 1));
                }
            }
    fixed.push_back(StokesSolver::dof(g.nodeId(0, 0, 0), 3));  // pressure datum
    stokes.setFixedDofs(fixed);

    const double alphaUniform = stokes.alpha(gammaUniform);
    const Vec gamma = Vec::Constant(g.nElems(), gammaUniform);
    const Vec zeroLoad = Vec::Zero(stokes.nDofTotal());
    const Vec U = useBrinkman ? stokes.solve({0.0, 0.0, kG}, zeroLoad, gamma)
                              : stokes.solve({0.0, 0.0, kG});

    DbResult r{};
    r.solveOk = U.allFinite();
    const bool db = useBrinkman && alphaUniform > 1e-30;
    r.uMax = db ? darcyBrinkman(kLx / 2.0, alphaUniform) : poiseuille(kLx / 2.0);

    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            for (int i = 0; i <= nelx; ++i) {
                const int n = g.nodeId(i, j, k);
                const double uz = U(StokesSolver::dof(n, 2));
                const double ex = db ? darcyBrinkman(i * h, alphaUniform)
                                     : poiseuille(i * h);
                r.relErr = std::max(r.relErr, std::fabs(uz - ex) / r.uMax);
                r.maxAbsUxy = std::max({r.maxAbsUxy,
                                        std::fabs(U(StokesSolver::dof(n, 0))),
                                        std::fabs(U(StokesSolver::dof(n, 1)))});
            }
    return r;
}

bool testDarcyBrinkman() {
    const double alpha = 16.0;             // κLx = √16 = 4 (μ=Lx=1)
    const double kappaL = std::sqrt(alpha / kMu) * kLx;
    // γ=0 -> α(0)=α_max uniform. Solid-everywhere porous channel.
    const DbResult r10 = runDarcyBrinkman(10, alpha, 0.0, true);
    const DbResult r20 = runDarcyBrinkman(20, alpha, 0.0, true);

    std::printf("[Darcy-Brinkman] alpha=%.1f  kappa*L=%.2f  u_max=%.4e\n",
                alpha, kappaL, r20.uMax);
    std::printf("  nx=10: relErr=%.3e  |u_xy|=%.2e\n", r10.relErr, r10.maxAbsUxy);
    std::printf("  nx=20: relErr=%.3e  |u_xy|=%.2e\n", r20.relErr, r20.maxAbsUxy);
    const double ratio = r20.relErr > 0 ? r10.relErr / r20.relErr : 0.0;
    std::printf("  error ratio (nx10/nx20)=%.2f (O(h^2) -> ~4)\n", ratio);

    const bool accurate = r20.relErr < 2e-2;
    const bool converges = r20.relErr < r10.relErr && ratio > 2.5;
    const bool clean = r10.maxAbsUxy < 1e-8 && r10.solveOk && r20.solveOk;

    // α→0 limit: γ=1 -> α(1)=α_min=0 -> pure Stokes Poiseuille parabola.
    const DbResult lim = runDarcyBrinkman(20, alpha, 1.0, true);
    std::printf("  alpha->0 limit (gamma=1): relErr vs Poiseuille=%.3e\n",
                lim.relErr);
    const bool limitOk = lim.relErr < 1e-2;

    std::printf("  -> accurate(<2%%)=%d converges=%d clean=%d limitPoiseuille=%d\n",
                accurate, converges, clean, limitOk);
    return accurate && converges && clean && limitOk;
}

// ---- ORACLE 2 : non-leak ----------------------------------------------------

// Solid slab on ex < nelx/2 (γ=0 -> α_max); fluid on ex >= nelx/2 (γ=1 -> ~0).
// Returns mean|u_z| over deep-solid nodes / mean|u_z| over fluid-interior nodes.
double leakRatio(int nelx, double alphaMax) {
    const int nely = 2, nelz = 2;
    const int split = nelx / 2;
    const double h = kLx / nelx;
    Grid3D g(nelx, nely, nelz);
    StokesSolver stokes(g, kMu, h, kAlphaStab);
    stokes.setBrinkman(alphaMax, 0.0, 0.1);

    std::vector<int> fixed;
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            for (int i = 0; i <= nelx; ++i) {
                const int n = g.nodeId(i, j, k);
                if (i == 0 || i == nelx) {
                    fixed.push_back(StokesSolver::dof(n, 0));
                    fixed.push_back(StokesSolver::dof(n, 1));
                    fixed.push_back(StokesSolver::dof(n, 2));
                } else if (j == 0 || j == nely) {
                    fixed.push_back(StokesSolver::dof(n, 1));
                }
            }
    fixed.push_back(StokesSolver::dof(g.nodeId(0, 0, 0), 3));
    stokes.setFixedDofs(fixed);

    Vec gamma = Vec::Zero(g.nElems());     // solid (γ=0) on lower-x half
    for (int ez = 0; ez < nelz; ++ez)
        for (int ey = 0; ey < nely; ++ey)
            for (int ex = 0; ex < nelx; ++ex)
                gamma(g.elemId(ex, ey, ez)) = ex < split ? 0.0 : 1.0;

    const Vec zeroLoad = Vec::Zero(stokes.nDofTotal());
    const Vec U = stokes.solve({0.0, 0.0, kG}, zeroLoad, gamma);
    if (!U.allFinite()) return 1e30;

    double solidSum = 0.0, fluidSum = 0.0;
    int solidCnt = 0, fluidCnt = 0;
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            for (int i = 0; i <= nelx; ++i) {
                const double uz =
                    std::fabs(U(StokesSolver::dof(g.nodeId(i, j, k), 2)));
                if (i >= 1 && i <= split - 1) {         // deep solid
                    solidSum += uz;
                    ++solidCnt;
                } else if (i >= split + 1 && i <= nelx - 1) {  // fluid interior
                    fluidSum += uz;
                    ++fluidCnt;
                }
            }
    const double solidMean = solidSum / solidCnt;
    const double fluidMean = fluidSum / fluidCnt;
    return solidMean / fluidMean;
}

bool testNonLeak() {
    const int nelx = 20;
    const double sweep[] = {1e1, 1e2, 1e3, 1e4, 1e5};
    std::printf("[non-leak] solid slab ex<%d, fluid ex>=%d (nelx=%d)\n",
                nelx / 2, nelx / 2, nelx);
    double sweet = 0.0;
    double leak1e1 = 0.0, leakBig = 0.0;
    for (double a : sweep) {
        const double lk = leakRatio(nelx, a);
        std::printf("  alpha_max=%.0e -> leak = %.3e (%.3f%%)%s\n", a, lk,
                    100.0 * lk, lk < 0.01 ? "  <1%% PASS" : "");
        if (lk < 0.01 && sweet == 0.0) sweet = a;
        if (a == 1e1) leak1e1 = lk;
        if (a == 1e5) leakBig = lk;
    }
    std::printf("  sweet spot (first alpha_max with leak<1%%) = %.0e\n", sweet);
    std::printf("  leak(1e1)/leak(1e5) = %.1f (small alpha_max -> leak up)\n",
                leakBig > 0 ? leak1e1 / leakBig : 0.0);

    const bool noLeak = sweet > 0.0;                 // some α_max achieves <1%
    const bool calibrates = leak1e1 > 10.0 * leakBig && leak1e1 > 0.1;
    std::printf("  -> noLeak(<1%%)=%d calibrationTrend=%d\n", noLeak, calibrates);
    return noLeak && calibrates;
}

} // namespace

int main() {
    int fails = 0;
    fails += testDarcyBrinkman() ? 0 : 1;
    fails += testNonLeak() ? 0 : 1;
    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    return fails;
}
