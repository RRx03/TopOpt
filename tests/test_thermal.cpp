// Thermal solver validation (GPU). Steady conduction -div(k grad T)=q.
//  (a) 1D slab patch test: cold face T=0, uniform flux on hot face, k=1 ->
//      linear field, T(L) = Qtot*L/(k*A) reproduced exactly (in float).
//  (b) linearity: doubling the total flux doubles the temperature field.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>

#include "core/Grid3D.hpp"
#include "fem/H8Element.hpp"
#include "physics/ThermalSolver.hpp"

using namespace topopt;
using namespace topopt::gpu;

namespace {

// Build a 1D-slab problem: cold face x=0 -> T=0 (Dirichlet), uniform total heat
// flux Qtot into hot face x=L. Returns fixed mask and nodal source Q.
void slabProblem(const Grid3D& g, double Qtot, std::vector<std::uint8_t>& fixed,
                 std::vector<float>& Q) {
    fixed.assign(static_cast<size_t>(g.nNodes()), 0);
    for (int k = 0; k <= g.nelz(); ++k)
        for (int j = 0; j <= g.nely(); ++j)
            fixed[static_cast<size_t>(g.nodeId(0, j, k))] = 1;  // x=0: T=0

    Q.assign(static_cast<size_t>(g.nNodes()), 0.0f);
    // Consistent nodal flux on x=L face (weights 1 interior, 0.5 edge, 0.25 corner).
    const double A = static_cast<double>(g.nely() * g.nelz());
    for (int k = 0; k <= g.nelz(); ++k)
        for (int j = 0; j <= g.nely(); ++j) {
            const double wj = (j == 0 || j == g.nely()) ? 0.5 : 1.0;
            const double wk = (k == 0 || k == g.nelz()) ? 0.5 : 1.0;
            Q[static_cast<size_t>(g.nodeId(g.nelx(), j, k))] =
                static_cast<float>(Qtot * wj * wk / A);
        }
}

} // namespace

int main() {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    const int nelx = 16, nely = 4, nelz = 4;
    Grid3D g(nelx, nely, nelz);

    MetalContext ctx;
    if (!ctx.valid()) { std::fprintf(stderr, "no Metal\n"); pool->release(); return 1; }
    ThermalSolver solver(ctx, g, H8Element::diffusion());
    if (!solver.valid()) { std::fprintf(stderr, "init failed\n"); pool->release(); return 1; }

    int fails = 0;
    std::vector<float> kcond(static_cast<size_t>(g.nElems()), 1.0f);  // k=1

    // (a) slab linear gradient vs analytical.
    {
        const double Qtot = 1.0;
        std::vector<std::uint8_t> fixed;
        std::vector<float> Q;
        slabProblem(g, Qtot, fixed, Q);
        std::vector<float> T(static_cast<size_t>(g.nNodes()), 0.0f);
        const auto r = solver.solve(kcond, Q, fixed, T, 5000, 1e-7f);

        const double A = static_cast<double>(nely * nelz);
        const double expected = Qtot * nelx / (1.0 * A);  // T(L) = Q*L/(k*A)
        double maxerr = 0.0;
        for (int k = 0; k <= nelz; ++k)
            for (int j = 0; j <= nely; ++j)
                maxerr = std::max(maxerr, std::fabs(
                    static_cast<double>(T[static_cast<size_t>(g.nodeId(nelx, j, k))]) - expected));
        const double rel = maxerr / expected;
        std::printf("[a] slab: T(L)_expected=%.6f maxerr=%.2e rel=%.2e (cg %d, relres %.1e)\n",
                    expected, maxerr, rel, r.iters, static_cast<double>(r.relResidual));
        fails += (r.converged && rel < 1e-3) ? 0 : 1;
    }

    // (b) linearity: 2x flux -> 2x field.
    {
        std::vector<std::uint8_t> fixed;
        std::vector<float> Q1, Q2;
        slabProblem(g, 1.0, fixed, Q1);
        slabProblem(g, 2.0, fixed, Q2);
        std::vector<float> T1(static_cast<size_t>(g.nNodes()), 0.0f);
        std::vector<float> T2(static_cast<size_t>(g.nNodes()), 0.0f);
        solver.solve(kcond, Q1, fixed, T1, 5000, 1e-7f);
        solver.solve(kcond, Q2, fixed, T2, 5000, 1e-7f);
        double maxdev = 0.0, maxv = 0.0;
        for (size_t i = 0; i < T1.size(); ++i) {
            maxdev = std::max(maxdev, std::fabs(static_cast<double>(T2[i] - 2.0f * T1[i])));
            maxv = std::max(maxv, std::fabs(static_cast<double>(T2[i])));
        }
        const double rel = maxdev / maxv;
        std::printf("[b] linearity: max|T2-2*T1|/max|T2| = %.2e\n", rel);
        fails += (rel < 1e-4) ? 0 : 1;
    }

    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    pool->release();
    return fails;
}
