// Weak thermo-elastic coupling validation (GPU elastic solve).
// Free thermal expansion: uniform temperature rise dT on a statically-determinate
// block, E=1, alpha given. The body expands stress-free with displacement field
// u = alpha*dT*x (linear -> exact in the FE space). We check u against that.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>

#include "core/Grid3D.hpp"
#include "fem/H8Element.hpp"
#include "gpu/CGSolver3D.hpp"
#include "physics/ThermoElasticCoupling.hpp"

using namespace topopt;
using namespace topopt::gpu;
using Vec = Eigen::VectorXd;

int main() {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    const int nelx = 8, nely = 6, nelz = 4;
    const double nu = 0.3, alpha = 1e-3, dT = 100.0, Tref = 0.0;
    Grid3D g(nelx, nely, nelz);

    // Statically determinate support: remove 3 translations + 3 rotations,
    // leave the block free to expand from the origin corner.
    std::vector<std::uint8_t> fixed(static_cast<size_t>(g.nDof()), 0);
    auto fix = [&](int dof) { fixed[static_cast<size_t>(dof)] = 1; };
    fix(3 * g.nodeId(0, 0, 0) + 0); fix(3 * g.nodeId(0, 0, 0) + 1); fix(3 * g.nodeId(0, 0, 0) + 2);
    fix(3 * g.nodeId(nelx, 0, 0) + 1); fix(3 * g.nodeId(nelx, 0, 0) + 2);
    fix(3 * g.nodeId(0, nely, 0) + 0); fix(3 * g.nodeId(0, nely, 0) + 2);
    fix(3 * g.nodeId(0, 0, nelz) + 0); fix(3 * g.nodeId(0, 0, nelz) + 1);

    // Uniform temperature field T = Tref + dT everywhere.
    Vec T = Vec::Constant(g.nNodes(), Tref + dT);
    const Vec Emod = Vec::Constant(g.nElems(), 1.0);

    ThermoElasticCoupling coupling(nu);
    const Vec Fth = coupling.thermalLoad(g, Emod, T, Tref, alpha);

    MetalContext ctx;
    if (!ctx.valid()) { std::fprintf(stderr, "no Metal\n"); pool->release(); return 1; }
    CGSolver3D solver(ctx, g, H8Element::stiffness(nu));
    if (!solver.valid()) { std::fprintf(stderr, "init failed\n"); pool->release(); return 1; }

    std::vector<float> Ef(static_cast<size_t>(g.nElems()), 1.0f);
    std::vector<float> Ff(static_cast<size_t>(g.nDof()));
    for (int d = 0; d < g.nDof(); ++d) Ff[static_cast<size_t>(d)] = static_cast<float>(Fth(d));
    std::vector<float> Uf(static_cast<size_t>(g.nDof()), 0.0f);

    const auto r = solver.solve(Ef, Ff, fixed, Uf, 5000, 1e-7f);

    // Expected u = alpha*dT*x (cell size 1 -> x = i, y = j, z = k).
    const double s = alpha * dT;
    double maxerr = 0.0, maxu = 0.0;
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            for (int i = 0; i <= nelx; ++i) {
                const int n = g.nodeId(i, j, k);
                const double ex[3] = {s * i, s * j, s * k};
                for (int c = 0; c < 3; ++c) {
                    maxu = std::max(maxu, std::fabs(ex[c]));
                    maxerr = std::max(maxerr,
                        std::fabs(static_cast<double>(Uf[static_cast<size_t>(3 * n + c)]) - ex[c]));
                }
            }
    const double rel = maxerr / maxu;
    std::printf("free expansion: u=alpha*dT*x, maxerr=%.2e rel=%.2e (cg %d, relres %.1e)\n",
                maxerr, rel, r.iters, static_cast<double>(r.relResidual));
    const bool ok = r.converged && rel < 1e-3;
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    pool->release();
    return ok ? 0 : 1;
}
