#include "topopt/MultiGridOptimizer.hpp"

#include <chrono>
#include <cstdio>
#include <memory>

#include "fem/H8Element.hpp"
#include "filter/HelmholtzFilterPhysical.hpp"
#include "gpu/CGSolver3D.hpp"
#include "topopt/GridTransfer.hpp"
#include "topopt/SIMP3D.hpp"

namespace topopt {

MultiGridOptimizer::Result MultiGridOptimizer::run(const BCBuilder& bc) {
    Result res;
    const auto KE0 = H8Element::stiffness(p_.nu);
    const auto t0 = std::chrono::steady_clock::now();

    Vec rho;          // design field on the current level
    Vec rhoPhys;      // filtered field
    double lastC = 0.0;

    for (int l = 0; l < mg_.nLevels(); ++l) {
        const Grid3D& grid = mg_.level(l);
        const int ne = grid.nElems();

        std::vector<std::uint8_t> fixedMask;
        Vec F;
        bc(grid, fixedMask, F);

        // Per-level GPU solver + physical filter (freed at scope exit -> peak
        // memory is the finest level only).
        gpu::CGSolver3D solver(ctx_, grid, KE0);
        HelmholtzFilterPhysical filter(ctx_, grid, p_.filterRadius_mm,
                                       mg_.cellSize(l));
        if (!solver.valid() || !filter.valid()) {
            std::fprintf(stderr, "MultiGridOptimizer: GPU init failed (level %d)\n", l);
            return res;
        }
        const auto filterFn = [&](const Vec& x) { return filter.apply(x); };

        // Warm-start: coarsest = uniform; finer = prolongate previous design.
        if (l == 0)
            rho = Vec::Constant(ne, p_.volfrac);
        else
            rho = GridTransfer::prolongateDensity(mg_.level(l - 1), grid, rho);
        rhoPhys = filterFn(rho);
        const Vec dv0 = filterFn(Vec::Ones(ne));

        std::vector<float> Ef(static_cast<size_t>(ne));
        std::vector<float> Ff(static_cast<size_t>(grid.nDof()));
        for (int d = 0; d < grid.nDof(); ++d)
            Ff[static_cast<size_t>(d)] = static_cast<float>(F(d));
        std::vector<float> Uf(static_cast<size_t>(grid.nDof()));
        std::vector<float> cef;

        if (p_.verbose)
            std::printf("--- level %d : %dx%dx%d (%d elems, h=%.3f mm, r=%.2f cells) ---\n",
                        l, grid.nelx(), grid.nely(), grid.nelz(), ne,
                        mg_.cellSize(l), filter.radiusCells());

        for (int it = 1; it <= p_.itersPerLevel; ++it) {
            const double penal = policy_.at(l, mg_.nLevels(), it).penal;
            SIMP3D simp(SIMP3D::Params{p_.E0, p_.Emin, penal, p_.volfrac, p_.move});

            const Vec E = simp.youngModulus(rhoPhys);
            for (int e = 0; e < ne; ++e)
                Ef[static_cast<size_t>(e)] = static_cast<float>(E(e));

            const auto r = solver.solve(Ef, Ff, fixedMask, Uf, p_.cgMaxIter, p_.cgTol);
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
            lastC = c;

            if (p_.verbose)
                std::printf("  L%d it%3d | p=%.2f | C=%.4f | vol=%.4f | cgit=%d\n",
                            l, it, penal, c, rhoPhys.mean(), r.iters);
        }
    }

    res.rhoPhysFine = rhoPhys;
    res.finalCompliance = lastC;
    res.seconds = std::chrono::duration<double>(
                      std::chrono::steady_clock::now() - t0).count();
    res.ok = true;
    return res;
}

} // namespace topopt
