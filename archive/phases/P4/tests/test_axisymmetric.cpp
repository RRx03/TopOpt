// Axisymmetric Q4 FEM validation against the Lame thick-cylinder solution
// (internal pressure only), CPU double precision.
//
// Case: a=1, b=2, p_i=1, E=1, nu=0.3, plane strain (u_z=0 on z=0 and z=H).
//   A = p_i a^2/(b^2-a^2),  B = p_i a^2 b^2/(b^2-a^2)
//   sigma_theta(r) = A + B/r^2     (independent of nu)
//   u_r(r) = ((1+nu)/E)[(1-2nu) A r + B/r]
// PASS if the max relative error on sigma_theta and u_r is < 2% at nr=40, and
// the error decreases under refinement (nr=20 -> nr=40).
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Grid2DAxi.hpp"
#include "fem/AxiQ4Element.hpp"
#include "fem/FEM2DAxi.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

namespace {

struct Errors {
    double sigma;  // max relative error on sigma_theta (element centroids)
    double ur;     // max relative error on u_r (nodes)
};

Errors runLame(int nr) {
    const double a = 1.0, b = 2.0, H = 1.0, p_i = 1.0;
    const double nu = 0.3, E0 = 1.0;
    const int nz = 2;

    const double A = p_i * a * a / (b * b - a * a);
    const double B = p_i * a * a * b * b / (b * b - a * a);

    Grid2DAxi g(nr, nz, a, b, H);
    FEM2DAxi fem(g, nu);

    // Plane strain: u_z = 0 on z=0 and z=H.
    std::vector<int> fixed;
    for (int i = 0; i <= nr; ++i) {
        fixed.push_back(2 * g.nodeId(i, 0) + 1);
        fixed.push_back(2 * g.nodeId(i, nz) + 1);
    }
    fem.setFixedDofs(fixed);

    const Vec E = Vec::Constant(g.nElems(), E0);
    const Vec F = fem.pressureLoadInner(p_i);
    const Vec U = fem.solve(E, F);

    // sigma_theta at element centroids vs Lame.
    const auto stress = fem.elementStress(E, U);
    double esig = 0.0;
    for (int ej = 0; ej < g.nz(); ++ej)
        for (int ei = 0; ei < g.nr(); ++ei) {
            const double rc = a + (ei + 0.5) * g.hr();
            const double exact = A + B / (rc * rc);
            const double fe = stress[static_cast<size_t>(g.elemId(ei, ej))](2);
            esig = std::max(esig, std::fabs(fe - exact) / std::fabs(exact));
        }

    // u_r at nodes vs Lame.
    double eur = 0.0;
    const double cu = (1.0 + nu) / E0;
    for (int j = 0; j <= nz; ++j)
        for (int i = 0; i <= nr; ++i) {
            const double r = g.r(i);
            const double exact = cu * ((1.0 - 2.0 * nu) * A * r + B / r);
            const double fe = U(2 * g.nodeId(i, j) + 0);
            eur = std::max(eur, std::fabs(fe - exact) / std::fabs(exact));
        }

    return {esig, eur};
}

} // namespace

int main() {
    const Errors e20 = runLame(20);
    const Errors e40 = runLame(40);

    std::printf("Lame thick cylinder (a=1,b=2,p_i=1,E=1,nu=0.3, plane strain)\n");
    std::printf("  nr=20 : sigma_theta err=%.4e   u_r err=%.4e\n",
                e20.sigma, e20.ur);
    std::printf("  nr=40 : sigma_theta err=%.4e   u_r err=%.4e\n",
                e40.sigma, e40.ur);
    std::printf("  convergence sigma: %.2fx   u_r: %.2fx\n",
                e20.sigma / e40.sigma, e20.ur / e40.ur);

    int fails = 0;
    fails += (e40.sigma < 2e-2) ? 0 : 1;       // sigma_theta < 2% at nr=40
    fails += (e40.ur < 2e-2) ? 0 : 1;          // u_r < 2% at nr=40
    fails += (e40.sigma < e20.sigma) ? 0 : 1;  // converges under refinement
    fails += (e40.ur < e20.ur) ? 0 : 1;

    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    return fails;
}
