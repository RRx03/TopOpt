#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Grid2D.hpp"
#include "fem/FEM2D.hpp"
#include "filter/Helmholtz.hpp"
#include "io/PNGWriter.hpp"
#include "topopt/SIMP.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

namespace {

struct Problem {
    std::string name = "mbb";
    int nelx = 200, nely = 100, max_iter = 100;
    double volfrac = 0.5, penal = 3.0, filter_radius = 2.0, move = 0.2;
    double E0 = 1.0, Emin = 1e-9, nu = 0.3;
};

Problem loadProblem(const std::string& path) {
    Problem p;
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "warning: cannot open %s, using defaults\n",
                     path.c_str());
        return p;
    }
    const nlohmann::json j = nlohmann::json::parse(f);
    p.name = j.value("name", p.name);
    p.nelx = j.value("nelx", p.nelx);
    p.nely = j.value("nely", p.nely);
    p.max_iter = j.value("max_iter", p.max_iter);
    p.volfrac = j.value("volfrac", p.volfrac);
    p.penal = j.value("penal", p.penal);
    p.filter_radius = j.value("filter_radius", p.filter_radius);
    p.move = j.value("move", p.move);
    p.E0 = j.value("E0", p.E0);
    p.Emin = j.value("Emin", p.Emin);
    p.nu = j.value("nu", p.nu);
    return p;
}

// Classic MBB beam (half domain by symmetry):
//   - left edge: ux = 0 (symmetry plane)
//   - bottom-right corner node: uy = 0 (roller)
//   - load: vertical downward unit force at top-left node
std::vector<int> mbbFixedDofs(const Grid2D& g) {
    std::vector<int> fixed;
    for (int row = 0; row <= g.nely(); ++row)
        fixed.push_back(2 * g.nodeId(0, row));            // ux on left edge
    fixed.push_back(2 * g.nodeId(g.nelx(), g.nely()) + 1);  // uy bottom-right
    return fixed;
}

Vec mbbLoad(const Grid2D& g) {
    Vec F = Vec::Zero(g.nDof());
    F(2 * g.nodeId(0, 0) + 1) = -1.0;  // downward at top-left
    return F;
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = (argc > 1) ? argv[1] : "mbb";
    if (mode != "mbb") {
        std::fprintf(stderr, "usage: %s mbb\n", argv[0]);
        return 2;
    }

    const Problem p = loadProblem("assets/problem_mbb.json");
    std::filesystem::create_directories("output");

    Grid2D grid(p.nelx, p.nely);
    FEM2D fem(grid, p.nu);
    fem.setFixedDofs(mbbFixedDofs(grid));
    const Vec F = mbbLoad(grid);

    SIMP simp(SIMP::Params{p.E0, p.Emin, p.penal, p.volfrac, p.move});
    Helmholtz filter(grid, p.filter_radius);
    const auto filterFn = [&](const Vec& x) { return filter.apply(x); };

    const int ne = grid.nElems();
    Vec rho = Vec::Constant(ne, p.volfrac);
    Vec rhoPhys = filterFn(rho);
    Vec ce(ne);
    const Vec dv0 = filterFn(Vec::Ones(ne));  // filtered volume sensitivity

    std::printf("MBB %dx%d  vol=%.2f  p=%.1f  R=%.1f  maxit=%d\n",
                p.nelx, p.nely, p.volfrac, p.penal, p.filter_radius, p.max_iter);
    std::printf("%4s | %12s | %7s | %8s\n", "it", "compliance", "vol", "change");

    const auto t0 = std::chrono::steady_clock::now();
    double change = 1.0;
    int it = 0;
    for (it = 1; it <= p.max_iter && change > 0.01; ++it) {
        const Vec E = simp.youngModulus(rhoPhys);
        const Vec U = fem.solve(E, F);

        double c = 0.0;
        for (int elx = 0; elx < grid.nelx(); ++elx)
            for (int ely = 0; ely < grid.nely(); ++ely) {
                const int e = grid.elemId(elx, ely);
                ce(e) = fem.elementStrainEnergy(U, elx, ely);
                c += E(e) * ce(e);
            }

        Vec dc = filterFn(simp.complianceSensitivity(rhoPhys, ce));
        const SIMP::OCResult oc = simp.ocUpdate(rho, dc, dv0, filterFn);

        change = (oc.rho - rho).cwiseAbs().maxCoeff();
        rho = oc.rho;
        rhoPhys = oc.rhoPhys;
        const double vol = rhoPhys.mean();

        std::printf("%4d | %12.4f | %7.4f | %8.4f\n", it, c, vol, change);

        if (it % 10 == 0) {
            char path[64];
            std::snprintf(path, sizeof(path), "output/iter_%03d.png", it);
            PNGWriter::writeDensity(path, rhoPhys, p.nelx, p.nely);
        }
    }

    PNGWriter::writeDensity("output/final.png", rhoPhys, p.nelx, p.nely);
    const auto t1 = std::chrono::steady_clock::now();
    const double secs =
        std::chrono::duration<double>(t1 - t0).count();
    std::printf("done: %d iterations in %.2fs -> output/final.png\n", it - 1,
                secs);
    return 0;
}
