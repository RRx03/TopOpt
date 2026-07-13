// Short 3D MBB topology-optimization run on the GPU: checks that the filter
// preserves a uniform field, that compliance decreases, and that the volume
// constraint is held.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>

#include "core/Grid3D.hpp"
#include "fem/H8Element.hpp"
#include "filter/Helmholtz3D.hpp"
#include "gpu/CGSolver3D.hpp"
#include "topopt/SIMP3D.hpp"

using namespace topopt;
using namespace topopt::gpu;
using Vec = Eigen::VectorXd;

int main() {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    const int nelx = 24, nely = 8, nelz = 8, iters = 15;
    const double volfrac = 0.3, nu = 0.3;
    Grid3D g(nelx, nely, nelz);

    gpu::MetalContext ctx;
    if (!ctx.valid()) { std::fprintf(stderr, "no Metal\n"); pool->release(); return 1; }
    CGSolver3D solver(ctx, g, H8Element::stiffness(nu));
    Helmholtz3D filter(ctx, g, 1.5);
    if (!solver.valid() || !filter.valid()) {
        std::fprintf(stderr, "init failed\n"); pool->release(); return 1;
    }
    const auto filterFn = [&](const Vec& x) { return filter.apply(x); };

    int fails = 0;

    // (a) filter preserves a uniform field.
    {
        const Vec x = Vec::Constant(g.nElems(), 0.5);
        const Vec y = filterFn(x);
        const double err = (y - x).cwiseAbs().maxCoeff();
        std::printf("[a] filter uniform: maxerr=%.2e\n", err);
        fails += (err < 1e-4) ? 0 : 1;
    }

    // (b) short MBB run: compliance decreases, volume held.
    std::vector<std::uint8_t> fixedMask(static_cast<size_t>(g.nDof()), 0);
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            fixedMask[static_cast<size_t>(3 * g.nodeId(0, j, k) + 0)] = 1;
    for (int j = 0; j <= nely; ++j)
        for (int i = 0; i <= nelx; ++i)
            fixedMask[static_cast<size_t>(3 * g.nodeId(i, j, 0) + 2)] = 1;
    for (int k = 0; k <= nelz; ++k)
        fixedMask[static_cast<size_t>(3 * g.nodeId(nelx, 0, k) + 1)] = 1;

    Vec F = Vec::Zero(g.nDof());
    for (int k = 0; k <= nelz; ++k)
        F(3 * g.nodeId(0, nely, k) + 1) = -1.0 / (nelz + 1);

    std::vector<float> Ff(static_cast<size_t>(g.nDof()));
    for (int d = 0; d < g.nDof(); ++d) Ff[static_cast<size_t>(d)] = static_cast<float>(F(d));

    SIMP3D simp(SIMP3D::Params{1.0, 1e-9, 3.0, volfrac, 0.2});
    const int ne = g.nElems();
    Vec rho = Vec::Constant(ne, volfrac);
    Vec rhoPhys = filterFn(rho);
    const Vec dv0 = filterFn(Vec::Ones(ne));

    std::vector<float> Ef(static_cast<size_t>(ne)), Uf(static_cast<size_t>(g.nDof())), cef;
    double c1 = 0.0, cN = 0.0, vol = 0.0;
    for (int it = 1; it <= iters; ++it) {
        const Vec E = simp.youngModulus(rhoPhys);
        for (int e = 0; e < ne; ++e) Ef[static_cast<size_t>(e)] = static_cast<float>(E(e));
        solver.solve(Ef, Ff, fixedMask, Uf, 4000, 1e-4f);
        solver.strainEnergy(cef);
        Vec ce(ne);
        double c = 0.0;
        for (int e = 0; e < ne; ++e) {
            ce(e) = static_cast<double>(cef[static_cast<size_t>(e)]);
            c += E(e) * ce(e);
        }
        const Vec dc = filterFn(simp.complianceSensitivity(rhoPhys, ce));
        const SIMP3D::OCResult oc = simp.ocUpdate(rho, dc, dv0, filterFn);
        rho = oc.rho;
        rhoPhys = oc.rhoPhys;
        vol = rhoPhys.mean();
        if (it == 1) c1 = c;
        cN = c;
    }
    std::printf("[b] MBB3D short: c1=%.4f cN=%.4f vol=%.4f\n", c1, cN, vol);
    fails += (cN < c1 && std::fabs(vol - volfrac) < 0.02) ? 0 : 1;

    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    pool->release();
    return fails;
}
