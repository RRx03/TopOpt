// CPU CHT validation (no Metal), double precision. Steady advection-diffusion
// of temperature with SUPG stabilisation:  −∇·(k∇T) + u·∇T = Q.
//
//  ORACLE 1 — advection-diffusion 1D. Domain [0,L] in x (invariant in y,z),
//    uniform velocity u=(a,0,0), uniform k, Q=0, T(0)=0, T(L)=1. Analytic
//        T(x) = (exp(Pe·x/L) − 1) / (exp(Pe) − 1),  Pe = a L / k.
//    Moderate Pe≈5 (a clear outflow boundary layer near x=L). PASS if the nodal
//    error < 2% at nx=20, the interior (mid-element) error converges ~O(h²), and
//    the field does not oscillate (monotone, bounded in [0,1]) with SUPG.
//    A second high-Péclet case (Pe≈50, coarse mesh, element Pe_e≫1) shows that
//    WITHOUT SUPG the Galerkin field overshoots/undershoots and WITH SUPG it
//    stays clean — the classic advection trap.
//
//  ORACLE 2 — conduction limit (u=0). −∇·(k∇T)=0 with T(0)=0, T(L)=1 gives the
//    linear profile T(x)=x/L, reproduced to < 1e-6 (consistent with P4).
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Grid3D.hpp"
#include "physics/CHTSolver.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

namespace {

constexpr double kL = 1.0;  // domain length in x

double analyticT(double x, double Pe) {
    return (std::exp(Pe * x / kL) - 1.0) / (std::exp(Pe) - 1.0);
}

struct AdvDiff {
    double nodalErr;     // max |T_fem − T_exact| over nodes
    double interiorErr;  // max mid-element error (O(h²) interpolation probe)
    double tMin, tMax;   // range (oscillation probe: exact stays in [0,1])
    double maxWiggle;    // max non-monotone dip along x (0 if monotone)
};

// Solve the 1D advection-diffusion channel on nx x 1 x 1 cube cells.
// a>0 uniform in x, k uniform, Dirichlet T=0 at x=0 and T=1 at x=L.
AdvDiff runAdvDiff(int nx, double Pe, bool supg) {
    const int ny = 1, nz = 1;
    const double h = kL / nx;
    const double a = 1.0;             // velocity magnitude
    const double k = a * kL / Pe;     // -> Pe = a L / k
    Grid3D gd(nx, ny, nz);
    CHTSolver cht(gd, h);

    const int nN = gd.nNodes();
    std::vector<double> kElem(static_cast<size_t>(gd.nElems()), k);
    Vec vel = Vec::Zero(3 * nN);
    for (int n = 0; n < nN; ++n) vel(3 * n + 0) = a;  // u=(a,0,0)
    Vec Q = Vec::Zero(nN);
    std::vector<std::uint8_t> mask(static_cast<size_t>(nN), 0);
    Vec dval = Vec::Zero(nN);
    for (int kk = 0; kk <= nz; ++kk)
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i) {
                const int n = gd.nodeId(i, j, kk);
                if (i == 0) { mask[static_cast<size_t>(n)] = 1; dval(n) = 0.0; }
                else if (i == nx) { mask[static_cast<size_t>(n)] = 1; dval(n) = 1.0; }
            }

    const Vec T = cht.solve(kElem, vel, Q, mask, dval, supg);

    AdvDiff r{0.0, 0.0, 1e300, -1e300, 0.0};
    for (int n = 0; n < nN; ++n) {
        r.tMin = std::min(r.tMin, T(n));
        r.tMax = std::max(r.tMax, T(n));
    }
    // Nodal error and monotonicity along the central x-line (j=k=0 here).
    std::vector<double> line(static_cast<size_t>(nx + 1));
    for (int i = 0; i <= nx; ++i) {
        const int n = gd.nodeId(i, 0, 0);
        line[static_cast<size_t>(i)] = T(n);
        r.nodalErr = std::max(r.nodalErr, std::fabs(T(n) - analyticT(i * h, Pe)));
    }
    for (int i = 1; i <= nx; ++i) {
        const double d = line[static_cast<size_t>(i)] - line[static_cast<size_t>(i - 1)];
        if (d < 0.0) r.maxWiggle = std::max(r.maxWiggle, -d);  // exact is monotone up
    }
    // Interior (mid-element) interpolation error: linear-in-element vs exact.
    for (int i = 0; i < nx; ++i) {
        const double mid = 0.5 * (line[static_cast<size_t>(i)] +
                                  line[static_cast<size_t>(i + 1)]);
        r.interiorErr = std::max(r.interiorErr,
                                 std::fabs(mid - analyticT((i + 0.5) * h, Pe)));
    }
    return r;
}

bool testAdvDiffModerate() {
    const double Pe = 5.0;
    const AdvDiff r10 = runAdvDiff(10, Pe, true);
    const AdvDiff r20 = runAdvDiff(20, Pe, true);
    const AdvDiff r40 = runAdvDiff(40, Pe, true);

    const double ratio =
        r40.interiorErr > 0 ? r20.interiorErr / r40.interiorErr : 0.0;
    std::printf("[adv-diff Pe=%.1f, SUPG]\n", Pe);
    std::printf("  nx=10: nodalErr=%.3e interiorErr=%.3e wiggle=%.2e range=[%.4f,%.4f]\n",
                r10.nodalErr, r10.interiorErr, r10.maxWiggle, r10.tMin, r10.tMax);
    std::printf("  nx=20: nodalErr=%.3e interiorErr=%.3e wiggle=%.2e range=[%.4f,%.4f]\n",
                r20.nodalErr, r20.interiorErr, r20.maxWiggle, r20.tMin, r20.tMax);
    std::printf("  nx=40: nodalErr=%.3e interiorErr=%.3e\n",
                r40.nodalErr, r40.interiorErr);
    std::printf("  interior err ratio (nx20/nx40)=%.2f (O(h^2) -> ~4)\n", ratio);

    const bool accurate = r20.nodalErr < 2e-2;
    const bool converges = r40.interiorErr < r20.interiorErr && ratio > 3.0;
    const bool noOsc = r20.maxWiggle < 1e-9 && r20.tMin > -1e-6 &&
                       r20.tMax < 1.0 + 1e-6;
    std::printf("  -> accurate(<2%%)=%d  O(h^2)=%d  noOscillation=%d\n",
                accurate, converges, noOsc);
    return accurate && converges && noOsc;
}

// High-Péclet trap: on a coarse mesh (element Pe_e≫1) the plain Galerkin scheme
// oscillates (over/undershoot outside [0,1]); SUPG keeps the field clean.
bool testPecletTrap() {
    const double Pe = 50.0;
    const int nx = 10;  // element Pe_e = Pe/(2 nx) = 2.5
    const AdvDiff noSupg = runAdvDiff(nx, Pe, false);
    const AdvDiff withSupg = runAdvDiff(nx, Pe, true);

    std::printf("[Peclet trap Pe=%.0f, nx=%d, Pe_e=%.2f]\n", Pe, nx,
                Pe / (2.0 * nx));
    std::printf("  no SUPG : range=[%.4f,%.4f] wiggle=%.3e  (Galerkin)\n",
                noSupg.tMin, noSupg.tMax, noSupg.maxWiggle);
    std::printf("  SUPG    : range=[%.4f,%.4f] wiggle=%.3e nodalErr=%.3e\n",
                withSupg.tMin, withSupg.tMax, withSupg.maxWiggle,
                withSupg.nodalErr);

    // Galerkin must visibly oscillate (dip below 0 or wiggle); SUPG must not.
    const bool galerkinOscillates =
        noSupg.tMin < -1e-3 || noSupg.maxWiggle > 1e-3;
    const bool supgClean = withSupg.tMin > -1e-6 &&
                           withSupg.tMax < 1.0 + 1e-6 &&
                           withSupg.maxWiggle < 1e-9;
    std::printf("  -> GalerkinOscillates=%d  SUPGclean=%d\n",
                galerkinOscillates, supgClean);
    return galerkinOscillates && supgClean;
}

// ORACLE 2: pure conduction (u=0). Linear profile T(x)=x/L, exact to ~1e-6.
bool testConduction() {
    const int nx = 20, ny = 1, nz = 1;
    const double h = kL / nx;
    Grid3D gd(nx, ny, nz);
    CHTSolver cht(gd, h);

    const int nN = gd.nNodes();
    std::vector<double> kElem(static_cast<size_t>(gd.nElems()), 1.0);
    const Vec vel = Vec::Zero(3 * nN);  // u = 0
    const Vec Q = Vec::Zero(nN);
    std::vector<std::uint8_t> mask(static_cast<size_t>(nN), 0);
    Vec dval = Vec::Zero(nN);
    for (int kk = 0; kk <= nz; ++kk)
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i) {
                const int n = gd.nodeId(i, j, kk);
                if (i == 0) { mask[static_cast<size_t>(n)] = 1; dval(n) = 0.0; }
                else if (i == nx) { mask[static_cast<size_t>(n)] = 1; dval(n) = 1.0; }
            }

    const Vec T = cht.solve(kElem, vel, Q, mask, dval, true);
    double err = 0.0;
    for (int i = 0; i <= nx; ++i)
        err = std::max(err, std::fabs(T(gd.nodeId(i, 0, 0)) - (i * h) / kL));

    std::printf("[conduction u=0] max|T − x/L| = %.3e\n", err);
    const bool ok = err < 1e-6;
    std::printf("  -> linear(<1e-6)=%d\n", ok);
    return ok;
}

} // namespace

int main() {
    int fails = 0;
    fails += testConduction() ? 0 : 1;
    fails += testAdvDiffModerate() ? 0 : 1;
    fails += testPecletTrap() ? 0 : 1;
    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    return fails;
}
