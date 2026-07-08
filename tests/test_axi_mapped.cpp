// Sanity guardrail for the mapped-coordinate (body-fitted) axisymmetric path
// used by the profiled-nozzle demo (Phase 5R). A TRIVIAL mapping (constant
// r_in = a, r_out = b) must REPRODUCE the Lame thick-cylinder solution, proving
// that feeding per-node radii through Grid2DAxi::setNodeRadii + the profiled
// pressure load does not break the validated correctness.
//
// Case: a=1, b=2, p_i=1, E=1, nu=0.3, plane strain (u_z=0 on z=0 and z=H).
//   sigma_theta(r) = A + B/r^2,  A = p a^2/(b^2-a^2),  B = p a^2 b^2/(b^2-a^2)
//   u_r(r) = ((1+nu)/E)[(1-2nu) A r + B/r]
// PASS if: (i) with the mapped grid sigma_theta and u_r err < 2% at nr=40 and
// converge under refinement; (ii) the profiled pressure load equals the
// rectangular pressureLoadInner to machine precision for the trivial mapping.
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Grid2DAxi.hpp"
#include "fem/FEM2DAxi.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

namespace {

struct Errors {
    double sigma;
    double ur;
    double loadDiff;  // max |F_profiled - F_rectangular| (trivial mapping)
};

// Build a trivial (constant a,b) per-node radius map: rNode(i,j) = a + i*hr.
std::vector<double> trivialMap(const Grid2DAxi& g, double a, double b) {
    std::vector<double> rmap(static_cast<size_t>(g.nNodes()));
    const double hr = (b - a) / g.nr();
    for (int j = 0; j <= g.nz(); ++j)
        for (int i = 0; i <= g.nr(); ++i)
            rmap[static_cast<size_t>(g.nodeId(i, j))] = a + i * hr;
    return rmap;
}

Errors runLameMapped(int nr) {
    const double a = 1.0, b = 2.0, H = 1.0, p_i = 1.0;
    const double nu = 0.3, E0 = 1.0;
    const int nz = 2;

    const double A = p_i * a * a / (b * b - a * a);
    const double B = p_i * a * a * b * b / (b * b - a * a);

    Grid2DAxi g(nr, nz, a, b, H);
    g.setNodeRadii(trivialMap(g, a, b));  // <-- exercise the mapped path

    FEM2DAxi fem(g, nu);
    std::vector<int> fixed;
    for (int i = 0; i <= nr; ++i) {
        fixed.push_back(2 * g.nodeId(i, 0) + 1);
        fixed.push_back(2 * g.nodeId(i, nz) + 1);
    }
    fem.setFixedDofs(fixed);

    // Profiled pressure load with a constant pressure profile.
    const std::vector<double> pRow(static_cast<size_t>(g.nzn()), p_i);
    const Vec Fprof = fem.pressureLoadInnerProfiled(pRow);
    const Vec Frect = fem.pressureLoadInner(p_i);
    const double loadDiff = (Fprof - Frect).cwiseAbs().maxCoeff();

    const Vec E = Vec::Constant(g.nElems(), E0);
    const Vec U = fem.solve(E, Fprof);

    const auto stress = fem.elementStress(E, U);
    double esig = 0.0;
    for (int ej = 0; ej < g.nz(); ++ej)
        for (int ei = 0; ei < g.nr(); ++ei) {
            const double rc = a + (ei + 0.5) * ((b - a) / nr);
            const double exact = A + B / (rc * rc);
            const double fe = stress[static_cast<size_t>(g.elemId(ei, ej))](2);
            esig = std::max(esig, std::fabs(fe - exact) / std::fabs(exact));
        }

    double eur = 0.0;
    const double cu = (1.0 + nu) / E0;
    for (int j = 0; j <= nz; ++j)
        for (int i = 0; i <= nr; ++i) {
            const double r = g.rNode(i, j);
            const double exact = cu * ((1.0 - 2.0 * nu) * A * r + B / r);
            const double fe = U(2 * g.nodeId(i, j) + 0);
            eur = std::max(eur, std::fabs(fe - exact) / std::fabs(exact));
        }

    return {esig, eur, loadDiff};
}

} // namespace

int main() {
    const Errors e20 = runLameMapped(20);
    const Errors e40 = runLameMapped(40);

    std::printf("Mapped-coordinate sanity: trivial mapping must reproduce Lame\n");
    std::printf("  (a=1,b=2,p_i=1,E=1,nu=0.3, plane strain, profiled load)\n");
    std::printf("  nr=20 : sigma_theta err=%.4e   u_r err=%.4e\n",
                e20.sigma, e20.ur);
    std::printf("  nr=40 : sigma_theta err=%.4e   u_r err=%.4e\n",
                e40.sigma, e40.ur);
    std::printf("  convergence sigma: %.2fx   u_r: %.2fx\n",
                e20.sigma / e40.sigma, e20.ur / e40.ur);
    std::printf("  profiled-vs-rectangular load max|dF| = %.3e (must be ~0)\n",
                e40.loadDiff);

    int fails = 0;
    fails += (e40.sigma < 2e-2) ? 0 : 1;
    fails += (e40.ur < 2e-2) ? 0 : 1;
    fails += (e40.sigma < e20.sigma) ? 0 : 1;
    fails += (e40.ur < e20.ur) ? 0 : 1;
    fails += (e40.loadDiff < 1e-12) ? 0 : 1;

    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    return fails;
}
