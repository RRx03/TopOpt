// Validate the GPU matrix-free CG solver against the CPU direct reference
// (SimplicialLDLT) on a 3D cantilever. float CG vs double direct: expect
// agreement to ~1e-3 relative.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>

#include "core/Grid3D.hpp"
#include "fem/FEM3D.hpp"
#include "fem/H8Element.hpp"
#include "gpu/CGSolver3D.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

int main() {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    const int nelx = 20, nely = 4, nelz = 4;
    const double nu = 0.3;
    Grid3D g(nelx, nely, nelz);

    // Clamp x=0 face, downward tip load on x=L face.
    std::vector<int> fixedDofs;
    std::vector<std::uint8_t> fixedMask(static_cast<size_t>(g.nDof()), 0);
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j) {
            const int n = g.nodeId(0, j, k);
            for (int c = 0; c < 3; ++c) {
                fixedDofs.push_back(3 * n + c);
                fixedMask[static_cast<size_t>(3 * n + c)] = 1;
            }
        }

    Vec F = Vec::Zero(g.nDof());
    const int ntip = (nely + 1) * (nelz + 1);
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            F(3 * g.nodeId(nelx, j, k) + 2) = -1.0e-3 / ntip;

    // CPU reference.
    FEM3D fem(g, nu);
    fem.setFixedDofs(fixedDofs);
    const Vec Evec = Vec::Constant(g.nElems(), 1.0);
    const Vec Ucpu = fem.solve(Evec, F);

    // GPU matrix-free CG.
    topopt::gpu::MetalContext ctx;
    if (!ctx.valid()) {
        std::fprintf(stderr, "ERROR: no Metal device\n");
        pool->release();
        return 1;
    }
    topopt::gpu::CGSolver3D cg(ctx, g, H8Element::stiffness(nu));
    if (!cg.valid()) {
        std::fprintf(stderr, "ERROR: CGSolver3D init failed\n");
        pool->release();
        return 1;
    }

    std::vector<float> Emodf(static_cast<size_t>(g.nElems()), 1.0f);
    std::vector<float> Ff(static_cast<size_t>(g.nDof()));
    for (int d = 0; d < g.nDof(); ++d) Ff[static_cast<size_t>(d)] = static_cast<float>(F(d));
    std::vector<float> Ugpu(static_cast<size_t>(g.nDof()), 0.0f);

    const auto r = cg.solve(Emodf, Ff, fixedMask, Ugpu, 5000, 1e-6f);

    double maxa = 0.0, maxd = 0.0;
    for (int d = 0; d < g.nDof(); ++d) {
        maxa = std::max(maxa, std::fabs(Ucpu(d)));
        maxd = std::max(maxd, std::fabs(static_cast<double>(Ugpu[static_cast<size_t>(d)]) - Ucpu(d)));
    }
    const double rel = maxd / maxa;
    std::printf("CG GPU: iters=%d  relres=%.2e  converged=%d\n", r.iters,
                static_cast<double>(r.relResidual), r.converged);
    std::printf("GPU vs CPU: max|du|/max|u| = %.2e (max|u|=%.3e)\n", rel, maxa);

    const bool ok = r.converged && rel < 1e-3;
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    pool->release();
    return ok ? 0 : 1;
}
