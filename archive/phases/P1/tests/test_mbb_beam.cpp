// Standalone tests (no framework): each returns true on success.
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Grid2D.hpp"
#include "fem/FEM2D.hpp"
#include "filter/Helmholtz.hpp"
#include "topopt/SIMP.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

namespace {

// (a) Tension bar vs analytical delta = F*L/(E*A).
bool testTensionBar() {
    const int nelx = 12, nely = 3;
    const double Ftot = 1.0;
    Grid2D g(nelx, nely);
    FEM2D fem(g, 0.3);

    std::vector<int> fixed;
    for (int row = 0; row <= nely; ++row) fixed.push_back(2 * g.nodeId(0, row));
    fixed.push_back(2 * g.nodeId(0, nely) + 1);
    fem.setFixedDofs(fixed);

    Vec F = Vec::Zero(g.nDof());
    for (int row = 0; row <= nely; ++row) {
        const double w = (row == 0 || row == nely) ? 0.5 : 1.0;
        F(2 * g.nodeId(nelx, row)) = Ftot * w / nely;
    }

    const Vec E = Vec::Constant(g.nElems(), 1.0);
    const Vec U = fem.solve(E, F);

    const double expected = Ftot * nelx / (1.0 * nely);
    double maxerr = 0.0;
    for (int row = 0; row <= nely; ++row)
        maxerr = std::max(maxerr,
                          std::fabs(U(2 * g.nodeId(nelx, row)) - expected));
    std::printf("[a] tension bar: expected=%.4f maxerr=%.2e\n", expected, maxerr);
    return maxerr < 1e-9;
}

// (b) Helmholtz filter preserves a uniform field.
bool testFilterUniform() {
    Grid2D g(40, 20);
    Helmholtz filter(g, 2.0);
    const Vec x = Vec::Constant(g.nElems(), 0.5);
    const Vec y = filter.apply(x);
    const double err = (y - x).cwiseAbs().maxCoeff();
    std::printf("[b] filter uniform: maxerr=%.2e\n", err);
    return err < 1e-10;
}

// (c) Short MBB run reduces compliance and holds the volume target.
bool testMbbShort() {
    const int nelx = 60, nely = 20, iters = 25;
    const double volfrac = 0.5;
    Grid2D g(nelx, nely);
    FEM2D fem(g, 0.3);

    std::vector<int> fixed;
    for (int row = 0; row <= nely; ++row) fixed.push_back(2 * g.nodeId(0, row));
    fixed.push_back(2 * g.nodeId(nelx, nely) + 1);
    fem.setFixedDofs(fixed);

    Vec F = Vec::Zero(g.nDof());
    F(2 * g.nodeId(0, 0) + 1) = -1.0;

    SIMP simp(SIMP::Params{1.0, 1e-9, 3.0, volfrac, 0.2});
    Helmholtz filter(g, 2.0);
    const auto filterFn = [&](const Vec& v) { return filter.apply(v); };

    const int ne = g.nElems();
    Vec rho = Vec::Constant(ne, volfrac);
    Vec rhoPhys = filterFn(rho);
    Vec ce(ne);
    const Vec dv0 = filterFn(Vec::Ones(ne));

    double c1 = 0.0, cN = 0.0, vol = 0.0;
    for (int it = 1; it <= iters; ++it) {
        const Vec E = simp.youngModulus(rhoPhys);
        const Vec U = fem.solve(E, F);
        double c = 0.0;
        for (int elx = 0; elx < nelx; ++elx)
            for (int ely = 0; ely < nely; ++ely) {
                const int e = g.elemId(elx, ely);
                ce(e) = fem.elementStrainEnergy(U, elx, ely);
                c += E(e) * ce(e);
            }
        const Vec dc = filterFn(simp.complianceSensitivity(rhoPhys, ce));
        const SIMP::OCResult oc = simp.ocUpdate(rho, dc, dv0, filterFn);
        rho = oc.rho;
        rhoPhys = oc.rhoPhys;
        vol = rhoPhys.mean();
        if (it == 1) c1 = c;
        cN = c;
    }
    std::printf("[c] MBB short: c1=%.3f cN=%.3f vol=%.4f\n", c1, cN, vol);
    return cN < c1 && std::fabs(vol - volfrac) < 0.01;
}

} // namespace

int main() {
    int fails = 0;
    fails += testTensionBar() ? 0 : 1;
    fails += testFilterUniform() ? 0 : 1;
    fails += testMbbShort() ? 0 : 1;
    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    return fails;
}
