// PHASE 5R (Borrvall-Petersson): dissipated-power objective adjoint gate.
//   Phi(gamma,u) = 1/2 ∫ mu|grad u|^2 + 1/2 ∫ alpha(gamma)|u|^2,
//   u = Stokes-Brinkman velocity.  dPhi/dgamma by discrete adjoint, validated
//   by central finite differences, entirely CPU double precision.
// Quasi-2D thin slab (slip z-faces -> 2D flow), imposed parabolic inlet
// velocity, free outlet, no-slip walls. PASS if max|adjoint-FD|/|FD| < 1e-4.
#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "adjoint/DissipationAdjoint.hpp"
#include "core/Grid3D.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

int main() {
    const int nx = 16, ny = 16, nz = 1;  // thin slab -> quasi-2D
    Grid3D g(nx, ny, nz);
    const int nN = g.nNodes();

    DissipationAdjoint::Params prm;
    prm.mu = 1.0;
    prm.alphaStab = 1.0 / 12.0;
    prm.alphaMax = 5.0e2;  // moderate Brinkman (1e2-1e3), well-conditioned
    prm.alphaMin = 0.0;
    prm.qBrink = 0.1;

    const std::array<double, 3> bodyForce = {0.0, 0.0, 0.0};  // driven by inlet

    // --- BCs. Channel along x; imposed parabolic inlet at x=0, free outlet at
    // x=nx; no-slip walls at y=0,ny; slip (u_z=0) on z-faces -> planar flow.
    // Pressure datum pinned at one corner node.
    std::vector<int> fixed;
    Vec dirVal = Vec::Zero(4 * nN);
    const double Umax = 1.0;
    for (int k = 0; k <= nz; ++k)
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i) {
                const int n = g.nodeId(i, j, k);
                const bool wall = (j == 0 || j == ny);
                const bool inlet = (i == 0);
                if (k == 0 || k == nz)
                    fixed.push_back(DissipationAdjoint::sdof(n, 2));  // slip
                if (wall) {
                    fixed.push_back(DissipationAdjoint::sdof(n, 0));
                    fixed.push_back(DissipationAdjoint::sdof(n, 1));
                    fixed.push_back(DissipationAdjoint::sdof(n, 2));
                } else if (inlet) {
                    const double yy = static_cast<double>(j) / ny;
                    const double ux = 4.0 * Umax * yy * (1.0 - yy);
                    fixed.push_back(DissipationAdjoint::sdof(n, 0));
                    fixed.push_back(DissipationAdjoint::sdof(n, 1));
                    fixed.push_back(DissipationAdjoint::sdof(n, 2));
                    dirVal(DissipationAdjoint::sdof(n, 0)) = ux;
                }
            }
    fixed.push_back(DissipationAdjoint::sdof(g.nodeId(0, 0, 0), 3));  // p datum

    DissipationAdjoint adj(g, prm, fixed, bodyForce, dirVal);

    // Random design in [0.3, 0.7] (fixed seed).
    std::mt19937 rng(12345u);
    std::uniform_real_distribution<double> dist(0.3, 0.7);
    Vec gamma(g.nElems());
    for (int e = 0; e < g.nElems(); ++e) gamma(e) = dist(rng);

    const auto sol = adj.solve(gamma);

    double umax = 0.0;
    for (int n = 0; n < nN; ++n) {
        const double ux = sol.w(4 * n + 0), uy = sol.w(4 * n + 1);
        umax = std::max(umax, std::sqrt(ux * ux + uy * uy));
    }
    std::printf("Phi = %.10e   max|u| = %.4f   (16x16x1 quasi-2D)\n", sol.Phi,
                umax);

    double sExp = 0.0, sAdj = 0.0;
    for (int e = 0; e < g.nElems(); ++e) {
        sExp += std::fabs(sol.termExplicit(e));
        sAdj += std::fabs(sol.termAdjoint(e));
    }
    std::printf("Sum|term_explicit| = %.6e   Sum|term_adjoint| = %.6e\n", sExp,
                sAdj);
    std::printf("self-adjoint check ||lambda_u + u|| / ||u|| = %.3e  "
                "(lambda_u ~ -u : %s)\n",
                sol.selfAdjResidual, sol.selfAdjResidual < 0.1 ? "yes" : "approx");

    // Central finite differences on random elements, eps = 1e-6.
    std::mt19937 pick(98765u);
    std::uniform_int_distribution<int> ielem(0, g.nElems() - 1);
    const double eps = 1e-6;
    const int nProbe = 18;

    std::printf("\n%4s %6s | %16s %16s | %10s | %12s %12s\n", "i", "elem",
                "adjoint", "FD", "rel.err", "t_explicit", "t_adjoint");

    double maxRel = 0.0, maxAbs = 0.0;
    for (int s = 0; s < nProbe; ++s) {
        const int e = ielem(pick);
        Vec gp = gamma, gm = gamma;
        gp(e) += eps;
        gm(e) -= eps;
        const double fd = (adj.objective(gp) - adj.objective(gm)) / (2.0 * eps);
        const double a = sol.grad(e);
        const double rel = std::fabs(a - fd) / std::max(std::fabs(fd), 1e-300);
        maxRel = std::max(maxRel, rel);
        maxAbs = std::max(maxAbs, std::fabs(a - fd));
        std::printf("%4d %6d | %16.9e %16.9e | %10.3e | %12.4e %12.4e\n", s, e,
                    a, fd, rel, sol.termExplicit(e), sol.termAdjoint(e));
    }

    const bool nontrivial = sExp > 1e-12 && sAdj > 1e-12;
    const bool pass = (maxRel < 1e-4) && nontrivial;
    std::printf("\nmax rel err = %.3e   max abs err = %.3e   eps = %.0e\n",
                maxRel, maxAbs, eps);
    std::printf("both terms non-trivial = %d\n", nontrivial);
    std::printf("-> %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
