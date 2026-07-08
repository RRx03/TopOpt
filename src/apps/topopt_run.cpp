// topopt_run — the input-language driver. Reads a .topopt.json problem spec,
// builds the problem (grid + boundary conditions via BCResolver), runs the
// optimization on the validated solver, and writes results (VTK for ParaView,
// STL via marching cubes). Closes the loop: author JSON -> run -> visualize.
//
// v1: structural (compliance/mass minimization under a volume constraint) on the
// GPU matrix-free path. v2/v3 (thermo, fluid) extend the physics dispatch.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>

#include "core/Grid3D.hpp"
#include "fem/H8Element.hpp"
#include "filter/Helmholtz3D.hpp"
#include "gpu/CGSolver3D.hpp"
#include "io/BCResolver.hpp"
#include "io/MarchingCubes.hpp"
#include "io/ProblemSpec.hpp"
#include "io/STLExporter.hpp"
#include "io/VTKExporter.hpp"
#include "topopt/SIMP3D.hpp"

using namespace topopt;
using namespace topopt::gpu;
using Vec = Eigen::VectorXd;

namespace {

double volumeConstraint(const ProblemSpec& s) {
    for (const auto& c : s.constraints)
        if (c.type == "volume") return c.max;
    return 0.5;
}

int runStructural(const ProblemSpec& s) {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    Grid3D grid(s.grid[0], s.grid[1], s.grid[2]);
    const int ne = grid.nElems();

    // Boundary conditions from the input language.
    const auto fixedMask = BCResolver::fixedMask(grid, s);
    const Vec F = BCResolver::loadVector(grid, s);

    MetalContext ctx;
    if (!ctx.valid()) { std::fprintf(stderr, "no Metal device\n"); pool->release(); return 1; }
    CGSolver3D solver(ctx, grid, H8Element::stiffness(s.nu));
    const double cell_mm = s.size_mm[0] / std::max(1, s.grid[0]);
    Helmholtz3D filter(ctx, grid, s.filter_radius_mm / cell_mm);
    if (!solver.valid() || !filter.valid()) {
        std::fprintf(stderr, "GPU init failed\n"); pool->release(); return 1;
    }
    const double volfrac = volumeConstraint(s);
    SIMP3D simp(SIMP3D::Params{s.E0, s.Emin, s.penal, volfrac, 0.2});
    const auto filterFn = [&](const Vec& x) { return filter.apply(x); };

    Vec rho = Vec::Constant(ne, volfrac);
    Vec rhoPhys = filterFn(rho);
    const Vec dv0 = filterFn(Vec::Ones(ne));

    std::vector<float> Ff(static_cast<size_t>(grid.nDof()));
    for (int d = 0; d < grid.nDof(); ++d) Ff[static_cast<size_t>(d)] = static_cast<float>(F(d));
    std::vector<float> Ef(static_cast<size_t>(ne)), Uf(static_cast<size_t>(grid.nDof())), cef;

    std::printf("topopt_run '%s': %dx%dx%d (%d elems), obj=%s vol<=%.2f, %d iter\n",
                s.name.c_str(), s.grid[0], s.grid[1], s.grid[2], ne,
                s.objective.c_str(), volfrac, s.max_iter);

    double change = 1.0, c = 0.0;
    for (int it = 1; it <= s.max_iter && change > 0.01; ++it) {
        const Vec E = simp.youngModulus(rhoPhys);
        for (int e = 0; e < ne; ++e) Ef[static_cast<size_t>(e)] = static_cast<float>(E(e));
        solver.solve(Ef, Ff, fixedMask, Uf, 4000, 1e-4f);
        solver.strainEnergy(cef);
        Vec ce(ne);
        c = 0.0;
        for (int e = 0; e < ne; ++e) {
            ce(e) = static_cast<double>(cef[static_cast<size_t>(e)]);
            c += E(e) * ce(e);
        }
        const Vec dc = filterFn(simp.complianceSensitivity(rhoPhys, ce));
        const SIMP3D::OCResult oc = simp.ocUpdate(rho, dc, dv0, filterFn);
        change = (oc.rho - rho).cwiseAbs().maxCoeff();
        rho = oc.rho;
        rhoPhys = oc.rhoPhys;
        if (it % 10 == 0 || it == 1)
            std::printf("  it %3d | C=%.4f | vol=%.4f | change=%.4f\n",
                        it, c, rhoPhys.mean(), change);
    }

    // Outputs (formats/dir from the spec).
    std::filesystem::create_directories(s.output_dir);
    const std::string base = s.output_dir + "/" + s.name;
    for (const auto& fmt : s.formats) {
        if (fmt == "vti") {
            VTKExporter::writeImageData(base + ".vti", grid, {{"density", &rhoPhys}});
        } else if (fmt == "stl") {
            if (s.stl_method == "marching_cubes") {
                std::vector<double> rhoElem(static_cast<size_t>(ne));
                for (int e = 0; e < ne; ++e) rhoElem[static_cast<size_t>(e)] = rhoPhys(e);
                const auto nodal = MarchingCubes::elementToNodal(rhoElem, grid);
                const auto tris = MarchingCubes::extract(nodal, grid, s.stl_iso);
                MarchingCubes::writeSTL(base + ".stl", tris);
            } else {
                STLExporter::writeVoxelSurface(base + ".stl", rhoPhys, grid, s.stl_iso);
            }
        }
    }
    std::printf("done: C=%.4f vol=%.4f -> %s.{%s}\n", c, rhoPhys.mean(), base.c_str(),
                s.formats.empty() ? "" : s.formats[0].c_str());
    pool->release();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <problem.topopt.json>\n", argv[0]);
        return 2;
    }
    ProblemSpec spec;
    try {
        spec = ProblemSpec::fromFile(argv[1]);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    // v1 dispatch: structural only. v2/v3 add thermal/fluid branches.
    const bool onlyElastic = spec.physics.size() == 1 && spec.physics[0] == "elastic";
    if (spec.dim == "3d" && onlyElastic) return runStructural(spec);
    std::fprintf(stderr,
                 "topopt_run v1 supports dim=3d physics=[elastic]; got dim=%s "
                 "physics[0]=%s. Thermal/fluid dispatch is v2/v3.\n",
                 spec.dim.c_str(), spec.physics.empty() ? "?" : spec.physics[0].c_str());
    return 3;
}
