// MMA optimiser validation (CPU pure, double precision).
//
//  ORACLE A (analytic, crisp): min f0 = sum c_j/x_j  s.t. (sum x_j)/n - vfrac <= 0,
//    x in [xmin,xmax]. Interior KKT optimum x_j* = n*vfrac*sqrt(c_j)/sum_k sqrt(c_k).
//    PASS if ||x_MMA - x*||_inf / ||x*||_inf < 1e-3.
//
//  ORACLE B (cross-check vs OC): 3D MBB compliance, 1 volume constraint, 24x8x8.
//    Same init / p / sensitivity filter for OC (SIMP3D) and MMA. CPU FEM (FEM3D,
//    direct LDLT). PASS if |c_MMA - c_OC|/c_OC < 5% and both hold the volume.
//
//  Plus a small m=2 sanity check exercising the projected-Newton dual path.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include <Eigen/Core>

#include "core/Grid3D.hpp"
#include "fem/FEM3D.hpp"
#include "problems/MBB3D.hpp"
#include "topopt/MMAOptimizer.hpp"
#include "topopt/SIMP3D.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;
using Mat = Eigen::MatrixXd;

// ---------------------------------------------------------------------------
// ORACLE A
// ---------------------------------------------------------------------------
static int oracleA() {
    const int n = 20;
    const double vfrac = 0.5;
    const double xmin = 1e-3, xmax = 1.0;

    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> dist(1.0, 10.0);
    Vec c(n);
    for (int j = 0; j < n; ++j) c(j) = dist(rng);

    // Analytic interior optimum: x_j* proportional to sqrt(c_j), mean = vfrac.
    Vec sc = c.array().sqrt();
    Vec xstar = (static_cast<double>(n) * vfrac / sc.sum()) * sc;

    const bool interior =
        (xstar.minCoeff() > xmin + 1e-9) && (xstar.maxCoeff() < xmax - 1e-9);

    MMAOptimizer::Params prm;            // defaults: move=0.5, dual=bisection (m=1)
    MMAOptimizer mma(n, 1, prm);

    Vec x = Vec::Constant(n, vfrac);
    Vec lo = Vec::Constant(n, xmin), hi = Vec::Constant(n, xmax);

    int iters = 0;
    double change = 1.0;
    for (; iters < 500 && change > 1e-12; ++iters) {
        const double f0 = (c.array() / x.array()).sum();
        const Vec df0 = -(c.array() / x.array().square()).matrix();
        Vec fv(1);
        fv(0) = x.sum() / n - vfrac;
        Mat df(1, n);
        for (int j = 0; j < n; ++j) df(0, j) = 1.0 / n;

        const Vec xn = mma.step(x, f0, df0, fv, df, lo, hi);
        change = (xn - x).cwiseAbs().maxCoeff();
        x = xn;
    }

    const double err = (x - xstar).cwiseAbs().maxCoeff() /
                       xstar.cwiseAbs().maxCoeff();
    const double vol = x.sum() / n;
    const bool pass = interior && (err < 1e-3);
    std::printf("[A] analytic: interior=%d iters=%d  rel_inf_err=%.3e  vol=%.4f (target %.2f)  -> %s\n",
                interior ? 1 : 0, iters, err, vol, vfrac, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// ---------------------------------------------------------------------------
// ORACLE B helpers: sensitivity filter (classic linear hat, radius rmin).
// ---------------------------------------------------------------------------
static std::vector<std::vector<std::pair<int, double>>>
buildFilter(const Grid3D& g, double rmin) {
    const int ne = g.nElems();
    std::vector<std::vector<std::pair<int, double>>> nb(static_cast<size_t>(ne));
    const int R = static_cast<int>(std::ceil(rmin));
    for (int ez = 0; ez < g.nelz(); ++ez)
    for (int ey = 0; ey < g.nely(); ++ey)
    for (int ex = 0; ex < g.nelx(); ++ex) {
        const int e = g.elemId(ex, ey, ez);
        for (int dz = -R; dz <= R; ++dz)
        for (int dy = -R; dy <= R; ++dy)
        for (int dx = -R; dx <= R; ++dx) {
            const int fx = ex + dx, fy = ey + dy, fz = ez + dz;
            if (fx < 0 || fy < 0 || fz < 0 ||
                fx >= g.nelx() || fy >= g.nely() || fz >= g.nelz())
                continue;
            const double dist = std::sqrt(static_cast<double>(dx*dx + dy*dy + dz*dz));
            const double w = rmin - dist;
            if (w > 0.0)
                nb[static_cast<size_t>(e)].emplace_back(g.elemId(fx, fy, fz), w);
        }
    }
    return nb;
}

// Sensitivity filter (Sigmund): dc_f_e = sum_f H_ef rho_f dc_f / (rho_e sum_f H_ef).
static Vec sensFilter(const std::vector<std::vector<std::pair<int,double>>>& nb,
                      const Vec& rho, const Vec& dc) {
    Vec out(dc.size());
    for (int e = 0; e < dc.size(); ++e) {
        double num = 0.0, den = 0.0;
        for (const auto& [f, w] : nb[static_cast<size_t>(e)]) {
            num += w * rho(f) * dc(f);
            den += w;
        }
        const double rhoe = std::max(rho(e), 1e-3);
        out(e) = num / (rhoe * den);
    }
    return out;
}

static double compliance(const FEM3D& fem, const Grid3D& g, const SIMP3D& simp,
                         const Vec& rho, const Vec& F, Vec& ceOut) {
    const Vec E = simp.youngModulus(rho);
    const Vec U = fem.solve(E, F);
    ceOut.resize(g.nElems());
    for (int ez = 0; ez < g.nelz(); ++ez)
    for (int ey = 0; ey < g.nely(); ++ey)
    for (int ex = 0; ex < g.nelx(); ++ex)
        ceOut(g.elemId(ex, ey, ez)) = fem.elementStrainEnergy(U, ex, ey, ez);
    return F.dot(U);
}

static int oracleB() {
    const int nelx = 24, nely = 8, nelz = 8;
    const double nu = 0.3, volfrac = 0.5, rmin = 1.5;
    Grid3D g(nelx, nely, nelz);
    const int ne = g.nElems();

    std::vector<std::uint8_t> mask;
    Vec F;
    mbb3dBoundary(g, mask, F);
    std::vector<int> fixed;
    for (int d = 0; d < g.nDof(); ++d)
        if (mask[static_cast<size_t>(d)]) fixed.push_back(d);

    FEM3D fem(g, nu);
    fem.setFixedDofs(fixed);

    SIMP3D::Params sp;
    sp.E0 = 1.0; sp.Emin = 1e-9; sp.penal = 3.0; sp.volfrac = volfrac; sp.move = 0.2;
    SIMP3D simp(sp);

    const auto nb = buildFilter(g, rmin);
    auto identity = [](const Vec& v) { return v; };

    const Vec rho0 = Vec::Constant(ne, volfrac);
    const int maxit = 80;

    // ---- OC run ----
    Vec rhoOC = rho0;
    double cOC = 0.0;
    {
        Vec ce;
        double change = 1.0;
        for (int it = 0; it < maxit && change > 1e-4; ++it) {
            cOC = compliance(fem, g, simp, rhoOC, F, ce);
            const Vec dcRaw = simp.complianceSensitivity(rhoOC, ce);
            const Vec dc = sensFilter(nb, rhoOC, dcRaw);
            const Vec dv = Vec::Ones(ne);
            const auto res = simp.ocUpdate(rhoOC, dc, dv, identity);
            change = (res.rho - rhoOC).cwiseAbs().maxCoeff();
            rhoOC = res.rho;
        }
    }

    // ---- MMA run (same problem, same filtered gradient) ----
    Vec rhoMMA = rho0;
    double cMMA = 0.0;
    MMAOptimizer::Params prm;
    prm.move = 0.2;
    MMAOptimizer mma(ne, 1, prm);
    Vec lo = Vec::Constant(ne, 1e-3), hi = Vec::Constant(ne, 1.0);
    {
        Vec ce;
        double change = 1.0;
        for (int it = 0; it < maxit && change > 1e-4; ++it) {
            cMMA = compliance(fem, g, simp, rhoMMA, F, ce);
            const Vec dcRaw = simp.complianceSensitivity(rhoMMA, ce);
            const Vec df0 = sensFilter(nb, rhoMMA, dcRaw);
            Vec fv(1);
            fv(0) = rhoMMA.sum() / ne - volfrac;
            Mat df(1, ne);
            for (int e = 0; e < ne; ++e) df(0, e) = 1.0 / ne;
            const Vec xn = mma.step(rhoMMA, cMMA, df0, fv, df, lo, hi);
            change = (xn - rhoMMA).cwiseAbs().maxCoeff();
            rhoMMA = xn;
        }
    }

    const double volOC = rhoOC.sum() / ne;
    const double volMMA = rhoMMA.sum() / ne;
    const double relc = std::abs(cMMA - cOC) / cOC;
    const bool volOk = std::abs(volMMA - volfrac) < 0.02 &&
                       std::abs(volOC - volfrac) < 0.02;
    const bool pass = (relc < 0.05) && volOk;
    std::printf("[B] MBB %dx%dx%d  c_OC=%.5f (vol %.4f)  c_MMA=%.5f (vol %.4f)  rel=%.3f%%  -> %s\n",
                nelx, nely, nelz, cOC, volOC, cMMA, volMMA, 100.0 * relc,
                pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

// ---------------------------------------------------------------------------
// m=2 sanity: min sum c_j/x_j s.t. mean(x)<=v1 AND mean(w_j x_j)<=v2.
// Exercises the projected-Newton dual path; checked against an independent
// projected-gradient reference solve of the SAME problem.
// ---------------------------------------------------------------------------
static int oracleNewton() {
    const int n = 12;
    const double xmin = 1e-3, xmax = 2.0;
    std::mt19937 rng(777);
    std::uniform_real_distribution<double> dc(1.0, 5.0), dw(0.5, 1.5);
    Vec c(n), w(n);
    for (int j = 0; j < n; ++j) { c(j) = dc(rng); w(j) = dw(rng); }
    const double v1 = 0.6, v2 = 0.6;

    MMAOptimizer mma(n, 2, {});
    Vec x = Vec::Constant(n, 0.5);
    Vec lo = Vec::Constant(n, xmin), hi = Vec::Constant(n, xmax);
    for (int it = 0; it < 400; ++it) {
        const double f0 = (c.array() / x.array()).sum();
        const Vec df0 = -(c.array() / x.array().square()).matrix();
        Vec fv(2);
        fv(0) = x.sum() / n - v1;
        fv(1) = (w.array() * x.array()).sum() / n - v2;
        Mat df(2, n);
        for (int j = 0; j < n; ++j) { df(0, j) = 1.0 / n; df(1, j) = w(j) / n; }
        const Vec xn = mma.step(x, f0, df0, fv, df, lo, hi);
        if ((xn - x).cwiseAbs().maxCoeff() < 1e-12) { x = xn; break; }
        x = xn;
    }

    // Independent reference: projected-gradient on the same constrained problem.
    Vec xr = Vec::Constant(n, 0.5);
    for (int it = 0; it < 200000; ++it) {
        const Vec gradf = -(c.array() / xr.array().square()).matrix();
        // crude penalty for the two inequality constraints
        const double g1 = xr.sum() / n - v1;
        const double g2 = (w.array() * xr.array()).sum() / n - v2;
        Vec g = gradf;
        const double mu = 5e3;
        if (g1 > 0) g += mu * g1 * (Vec::Ones(n) / n);
        if (g2 > 0) g += mu * g2 * (w / n);
        xr = (xr - 1e-4 * g).cwiseMax(xmin).cwiseMin(xmax);
    }

    const double f0_mma = (c.array() / x.array()).sum();
    const double f0_ref = (c.array() / xr.array()).sum();
    const double rel = std::abs(f0_mma - f0_ref) / std::abs(f0_ref);
    const bool feas = (x.sum() / n - v1) < 1e-3 &&
                      ((w.array() * x.array()).sum() / n - v2) < 1e-3;
    // Reference is a crude penalty solve, so allow some slack; MMA is the
    // accurate side here. The check guards the projected-Newton dual path.
    const bool pass = feas && rel < 3e-2;
    std::printf("[N] m=2 dual(Newton): f0_MMA=%.5f f0_ref=%.5f rel=%.3f%% feas=%d -> %s\n",
                f0_mma, f0_ref, 100.0 * rel, feas ? 1 : 0, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

int main() {
    int fails = 0;
    fails += oracleA();
    fails += oracleNewton();
    fails += oracleB();
    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    return fails;
}
