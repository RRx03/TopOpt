// Phase 4 GATE: discrete elastic adjoint of the axisymmetric stress p-norm
// sigma_PN, validated by central finite differences, entirely in CPU double
// precision (Eigen direct solves). Forward is a single axisymmetric Q4 solve
// under a fixed internal-pressure load; the gradient combines the explicit
// rho^q-relaxation term with the lam^T (dK/drho) U term carrying dsigma_PN/dU.
// CRITICAL: KE0ax is recomputed per element (axisymmetry -> r-dependent).
// PASS if max over 20 random elements of |adjoint - FD| / |FD| < 1e-5.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "adjoint/AxiStressAdjoint.hpp"
#include "core/Grid2DAxi.hpp"
#include "topopt/StressModelAxi.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

namespace {

// 4th-order central finite difference of sigma_PN w.r.t. rho(e).
double fd4(const AxiStressAdjoint& adj, const StressModelAxi& sm,
           const Vec& rho, int e, double eps) {
    Vec rp1 = rho, rm1 = rho, rp2 = rho, rm2 = rho;
    rp1(e) += eps;
    rm1(e) -= eps;
    rp2(e) += 2.0 * eps;
    rm2(e) -= 2.0 * eps;
    const double Jp1 = adj.stressPNorm(rp1, sm);
    const double Jm1 = adj.stressPNorm(rm1, sm);
    const double Jp2 = adj.stressPNorm(rp2, sm);
    const double Jm2 = adj.stressPNorm(rm2, sm);
    return (-Jp2 + 8.0 * Jp1 - 8.0 * Jm1 + Jm2) / (12.0 * eps);
}

} // namespace

int main() {
    // --- Case: ~10x10 axisymmetric annulus, internal pressure (plane strain). ---
    const int nr = 10, nz = 10;
    const double a = 1.0, b = 2.0, H = 2.0, p_i = 1.0;

    Grid2DAxi g(nr, nz, a, b, H);

    AxiStressAdjoint::Material mat;  // E0=1, Emin=1e-4, p=3, nu=0.3

    // BC: u_z = 0 on z=0 and z=H (plane strain). Pressure on r=a (handled by F).
    std::vector<int> fixed;
    for (int i = 0; i <= nr; ++i) {
        fixed.push_back(2 * g.nodeId(i, 0) + 1);
        fixed.push_back(2 * g.nodeId(i, nz) + 1);
    }

    // Fixed internal-pressure load on r=a.
    FEM2DAxi femLoad(g, mat.nu);  // only used to build the consistent load vector
    const Vec F = femLoad.pressureLoadInner(p_i);

    AxiStressAdjoint adj(g, mat, fixed, F);

    // Stress model: q-relaxation exponent q=0.5, aggregation P=8.
    StressModelAxi sm(mat.nu, /*qRelax=*/0.5, /*Pagg=*/8.0);

    // Random density in [0.3, 0.7] (fixed seed).
    std::mt19937 rng(12345u);
    std::uniform_real_distribution<double> dist(0.3, 0.7);
    Vec rho(g.nElems());
    for (int e = 0; e < g.nElems(); ++e) rho(e) = dist(rng);

    // Adjoint gradient of sigma_PN.
    const auto sol = adj.stressPNormGrad(rho, sm);
    std::printf(
        "sigma_PN = %.10e   (%dx%d axi, a=%.1f b=%.1f H=%.1f, P=%.0f, q=%.2f)\n",
        sol.J, nr, nz, a, b, H, sm.Pagg(), sm.qRelax());

    // Confirm both gradient channels are active.
    const double sumExpl = sol.termExplicit.cwiseAbs().sum();
    const double sumAdj = sol.termAdjoint.cwiseAbs().sum();
    std::printf("|explicit term| sum = %.3e   |dJ/dU adjoint term| sum = %.3e\n",
                sumExpl, sumAdj);

    // Spot-check 20 random elements with 4th-order central FD. A single FE solve
    // per evaluation carries ~1e-13 relative round-off; the 4th-order stencil
    // keeps truncation negligible so eps can be enlarged to push the round-off
    // floor (~machine*|J|/eps) far below the 1e-5 gate even on near-zero-gradient
    // elements (LL-009).
    std::mt19937 pick(98765u);
    std::uniform_int_distribution<int> ielem(0, g.nElems() - 1);
    const double eps = 1e-4;

    std::printf("\n%4s %6s | %15s %15s | %10s | %12s %12s\n", "i", "elem",
                "adjoint", "FD", "rel.err", "t_explicit", "t_adjoint");

    double maxRel = 0.0, maxAbs = 0.0;
    for (int s = 0; s < 20; ++s) {
        const int e = ielem(pick);
        const double fd = fd4(adj, sm, rho, e, eps);
        const double ad = sol.grad(e);
        const double rel = std::fabs(ad - fd) / std::max(std::fabs(fd), 1e-300);
        const double abserr = std::fabs(ad - fd);
        maxRel = std::max(maxRel, rel);
        maxAbs = std::max(maxAbs, abserr);
        std::printf("%4d %6d | %15.7e %15.7e | %10.3e | %12.4e %12.4e\n", s, e,
                    ad, fd, rel, sol.termExplicit(e), sol.termAdjoint(e));
    }

    std::printf("\nmax relative error = %.3e   max absolute error = %.3e\n",
                maxRel, maxAbs);

    // eps sweep diagnostic on the first picked element (round-off signature).
    {
        std::mt19937 pick2(98765u);
        std::uniform_int_distribution<int> ie(0, g.nElems() - 1);
        const int e0 = ie(pick2);
        const double ad = sol.grad(e0);
        std::printf("\neps sweep (elem %d, adjoint=%.7e):\n", e0, ad);
        for (double ee : {1e-6, 1e-5, 1e-4, 1e-3}) {
            const double fd = fd4(adj, sm, rho, e0, ee);
            std::printf("  eps=%.0e : FD=%.7e  rel.err=%.3e\n", ee, fd,
                        std::fabs(ad - fd) / std::max(std::fabs(fd), 1e-300));
        }
    }

    const bool pass = maxRel < 1e-5;
    std::printf("\n%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
