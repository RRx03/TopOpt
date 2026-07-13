#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>

#include "core/Grid3D.hpp"
#include "core/Grid3DMultiLevel.hpp"
#include "fem/H8Element.hpp"
#include "filter/Helmholtz3D.hpp"
#include "gpu/CGSolver3D.hpp"
#include "io/STLExporter.hpp"
#include "problems/MBB3D.hpp"
#include "topopt/ContinuationPolicy.hpp"
#include "topopt/MultiGridOptimizer.hpp"
#include "topopt/SIMP3D.hpp"

using namespace topopt;
using namespace topopt::gpu;
using Vec = Eigen::VectorXd;

namespace {

struct Problem {
    int nelx = 60, nely = 20, nelz = 20, max_iter = 60;
    double volfrac = 0.3, penal = 3.0, filter_radius = 1.5, move = 0.2;
    // Emin=1e-4 (not 1e-9 as in Phase 1): the iterative float32 CG needs a
    // bounded stiffness contrast or K becomes too ill-conditioned in the void
    // regions and Jacobi-PCG stalls (cf. LL-006). Direct solvers tolerate
    // 1e-9; matrix-free iterative ones do not.
    double E0 = 1.0, Emin = 1e-4, nu = 0.3;
    float cg_tol = 1e-4f;
    int cg_maxiter = 4000;
};

// Multi-grid warm-start optimization (Phase 3). Builds an L-level hierarchy from
// the finest dims and runs coarse->fine. Continuation mode selectable.
int runMultiGrid(int nelxFine, int nLevels, int itersPerLevel,
                 ContinuationPolicy::Mode mode) {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    const int nyFine = nelxFine, nzFine = nelxFine;  // cube (comparable to bench)
    if (!Grid3DMultiLevel::divisible(nelxFine, nyFine, nzFine, nLevels)) {
        std::fprintf(stderr, "dims %dx%dx%d not divisible by 2^%d\n", nelxFine,
                     nyFine, nzFine, nLevels - 1);
        pool->release();
        return 2;
    }
    std::filesystem::create_directories("output");

    Grid3DMultiLevel mg(nelxFine, nyFine, nzFine, nLevels, 60.0);  // 60 mm span
    gpu::MetalContext ctx;
    if (!ctx.valid()) { std::fprintf(stderr, "no Metal\n"); pool->release(); return 1; }

    MultiGridOptimizer::Params p;
    p.itersPerLevel = itersPerLevel;
    p.filterRadius_mm = 2.0;
    ContinuationPolicy policy(mode, /*penalMax=*/3.0, /*penalStart=*/1.0,
                              /*rampIters=*/itersPerLevel);
    MultiGridOptimizer opt(ctx, mg, p, policy);

    const char* modeName = mode == ContinuationPolicy::Mode::Inherit ? "inherit"
                         : mode == ContinuationPolicy::Mode::Restart ? "restart"
                                                                     : "custom";
    std::printf("MULTIGRID finest %dx%dx%d, %d levels, %d it/level, continuation=%s\n",
                nelxFine, nyFine, nzFine, nLevels, itersPerLevel, modeName);

    const auto res = opt.run(mbb3dBoundary);
    if (!res.ok) { pool->release(); return 1; }

    char path[64];
    std::snprintf(path, sizeof(path), "output/mg_%d.stl", nelxFine);
    STLExporter::writeVoxelSurface(path, res.rhoPhysFine, mg.finest(), 0.5);
    std::printf("done: C=%.4f in %.1fs -> %s\n", res.finalCompliance,
                res.seconds, path);
    pool->release();
    return 0;
}

// Benchmark a single FEM solve at n^3 with uniform density, reporting wall time,
// CG iterations and peak GPU working set.
int runBench(int n) {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    Problem p;
    Grid3D grid(n, n, n);   // canonical n^3 cube
    std::vector<std::uint8_t> fixedMask;
    Vec F;
    mbb3dBoundary(grid, fixedMask, F);

    gpu::MetalContext ctx;
    if (!ctx.valid()) { std::fprintf(stderr, "no Metal\n"); pool->release(); return 1; }
    CGSolver3D solver(ctx, grid, H8Element::stiffness(p.nu));
    if (!solver.valid()) { std::fprintf(stderr, "init failed\n"); pool->release(); return 1; }

    const int ne = grid.nElems();
    std::vector<float> Ef(static_cast<size_t>(ne), 0.0f);
    SIMP3D simp(SIMP3D::Params{p.E0, p.Emin, p.penal, p.volfrac, p.move});
    const Vec E = simp.youngModulus(Vec::Constant(ne, p.volfrac));
    for (int e = 0; e < ne; ++e) Ef[static_cast<size_t>(e)] = static_cast<float>(E(e));
    std::vector<float> Ff(static_cast<size_t>(grid.nDof()));
    for (int d = 0; d < grid.nDof(); ++d) Ff[static_cast<size_t>(d)] = static_cast<float>(F(d));
    std::vector<float> Uf(static_cast<size_t>(grid.nDof()));

    std::printf("BENCH %dx%dx%d : %d elems, %d dof, working set %.1f GB\n",
                grid.nelx(), grid.nely(), grid.nelz(), ne, grid.nDof(),
                static_cast<double>(ctx.caps().recommendedWorkingSetBytes) / 1e9);
    for (int s = 0; s < 3; ++s) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = solver.solve(Ef, Ff, fixedMask, Uf, 8000, 1e-4f);
        const double dt = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
        std::printf("  solve %d: %.2fs  cgit=%d  relres=%.2e  converged=%d\n",
                    s, dt, r.iters, static_cast<double>(r.relResidual), r.converged);
    }
    pool->release();
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    const std::string mode = (argc > 1) ? argv[1] : "mbb";
    if (mode == "bench") {
        const int n = (argc > 2) ? std::atoi(argv[2]) : 128;
        return runBench(n);
    }
    if (mode == "mg") {
        const int nFine = (argc > 2) ? std::atoi(argv[2]) : 96;
        const int nLevels = (argc > 3) ? std::atoi(argv[3]) : 3;
        const int itPer = (argc > 4) ? std::atoi(argv[4]) : 25;
        ContinuationPolicy::Mode cm = ContinuationPolicy::Mode::Inherit;
        if (argc > 5) {
            const std::string m = argv[5];
            if (m == "restart") cm = ContinuationPolicy::Mode::Restart;
            else if (m == "inherit") cm = ContinuationPolicy::Mode::Inherit;
        }
        return runMultiGrid(nFine, nLevels, itPer, cm);
    }
    if (mode != "mbb") {
        std::fprintf(stderr,
                     "usage: %s mbb [nelx nely nelz maxiter] | "
                     "mg [nFine nLevels itPerLevel inherit|restart] | bench [n]\n",
                     argv[0]);
        return 2;
    }
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    Problem p;
    if (argc >= 5) {
        p.nelx = std::atoi(argv[2]);
        p.nely = std::atoi(argv[3]);
        p.nelz = std::atoi(argv[4]);
    }
    if (argc >= 6) p.max_iter = std::atoi(argv[5]);
    std::filesystem::create_directories("output");

    Grid3D grid(p.nelx, p.nely, p.nelz);
    std::vector<std::uint8_t> fixedMask;
    Vec F;
    mbb3dBoundary(grid, fixedMask, F);

    gpu::MetalContext ctx;
    if (!ctx.valid()) {
        std::fprintf(stderr, "ERROR: no Metal device\n");
        pool->release();
        return 1;
    }
    CGSolver3D solver(ctx, grid, H8Element::stiffness(p.nu));
    Helmholtz3D filter(ctx, grid, p.filter_radius);
    if (!solver.valid() || !filter.valid()) {
        std::fprintf(stderr, "ERROR: GPU solver/filter init failed\n");
        pool->release();
        return 1;
    }

    SIMP3D simp(SIMP3D::Params{p.E0, p.Emin, p.penal, p.volfrac, p.move});
    const auto filterFn = [&](const Vec& x) { return filter.apply(x); };

    const int ne = grid.nElems();
    Vec rho = Vec::Constant(ne, p.volfrac);
    Vec rhoPhys = filterFn(rho);
    const Vec dv0 = filterFn(Vec::Ones(ne));

    std::vector<float> Ff(static_cast<size_t>(grid.nDof()));
    for (int d = 0; d < grid.nDof(); ++d) Ff[static_cast<size_t>(d)] = static_cast<float>(F(d));
    std::vector<float> Ef(static_cast<size_t>(ne));
    std::vector<float> Uf(static_cast<size_t>(grid.nDof()));
    std::vector<float> cef;

    std::printf("MBB3D %dx%dx%d (%d elems, %d dof)  vol=%.2f p=%.1f R=%.1f\n",
                p.nelx, p.nely, p.nelz, ne, grid.nDof(), p.volfrac, p.penal,
                p.filter_radius);
    std::printf("%4s | %12s | %7s | %8s | %5s\n", "it", "compliance", "vol",
                "change", "cgit");

    const auto t0 = std::chrono::steady_clock::now();
    double change = 1.0;
    int it = 0;
    for (it = 1; it <= p.max_iter && change > 0.01; ++it) {
        const Vec E = simp.youngModulus(rhoPhys);
        for (int e = 0; e < ne; ++e) Ef[static_cast<size_t>(e)] = static_cast<float>(E(e));

        const auto r = solver.solve(Ef, Ff, fixedMask, Uf, p.cg_maxiter, p.cg_tol);
        solver.strainEnergy(cef);

        Vec ce(ne);
        double c = 0.0;
        for (int e = 0; e < ne; ++e) {
            ce(e) = static_cast<double>(cef[static_cast<size_t>(e)]);
            c += E(e) * ce(e);
        }

        const Vec dc = filterFn(simp.complianceSensitivity(rhoPhys, ce));
        const SIMP3D::OCResult oc = simp.ocUpdate(rho, dc, dv0, filterFn);
        change = (oc.rho - rho).cwiseAbs().maxCoeff();
        rho = oc.rho;
        rhoPhys = oc.rhoPhys;

        std::printf("%4d | %12.4f | %7.4f | %8.4f | %5d\n", it, c,
                    rhoPhys.mean(), change, r.iters);
    }

    STLExporter::writeVoxelSurface("output/mbb3d.stl", rhoPhys, grid, 0.5);
    const auto t1 = std::chrono::steady_clock::now();
    std::printf("done: %d iterations in %.1fs -> output/mbb3d.stl\n", it - 1,
                std::chrono::duration<double>(t1 - t0).count());
    pool->release();
    return 0;
}
