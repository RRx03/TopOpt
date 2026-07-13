// Phase 4 GATE: discrete two-block adjoint of the stress p-norm sigma_PN,
// validated by central finite differences, entirely in CPU double precision
// (Eigen direct solves). Same forward + same two adjoint solves as the compliance
// gate; only the elastic adjoint RHS (-dJ/dU from stress) and the explicit
// rho^q-relaxation term differ.
// PASS if max over 20 random elements of |adjoint - FD| / |FD| < 1e-5.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "adjoint/ThermoElasticAdjoint.hpp"
#include "core/Grid3D.hpp"
#include "topopt/StressModel.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

int main() {
    // --- Case: 8x8x8 cube, one-way thermo-elastic (identical to compliance gate). ---
    const int nel = 8;
    Grid3D g(nel, nel, nel);

    ThermoElasticAdjoint::Material mat;  // E0=1,Emin=1e-4,p=3; k0=1,kmin=1e-4,q=3
    mat.alpha = 1e-3;
    mat.Tref = 0.0;
    mat.nu = 0.3;

    // BCs: clamp the x=0 face (3 DOFs) elastic; T=0 (Dirichlet) on x=0 thermal.
    std::vector<int> elasticFixed;
    std::vector<int> thermalFixed;
    for (int k = 0; k <= nel; ++k)
        for (int j = 0; j <= nel; ++j) {
            const int n = g.nodeId(0, j, k);
            elasticFixed.push_back(3 * n + 0);
            elasticFixed.push_back(3 * n + 1);
            elasticFixed.push_back(3 * n + 2);
            thermalFixed.push_back(n);
        }

    // Loads on the opposite (x=nel) face: downward shear + heat source.
    Vec Fmech = Vec::Zero(g.nDof());
    Vec Q = Vec::Zero(g.nNodes());
    for (int k = 0; k <= nel; ++k)
        for (int j = 0; j <= nel; ++j) {
            const int n = g.nodeId(nel, j, k);
            Fmech(3 * n + 2) = -1e-2;
            Q(n) = 5.0;
        }

    ThermoElasticAdjoint adj(g, mat, elasticFixed, thermalFixed, Fmech, Q);

    // Stress model: q-relaxation exponent q=0.5, aggregation P=8.
    StressModel sm(mat.nu, /*qRelax=*/0.5, /*Pagg=*/8.0);

    // Random density in [0.3, 0.7] (fixed seed).
    std::mt19937 rng(12345u);
    std::uniform_real_distribution<double> dist(0.3, 0.7);
    Vec rho(g.nElems());
    for (int e = 0; e < g.nElems(); ++e) rho(e) = dist(rng);

    // Adjoint gradient of sigma_PN.
    const auto sol = adj.stressPNormGrad(rho, sm);
    std::printf("sigma_PN = %.10e   (8x8x8, P=%.0f, q=%.2f, alpha=%.1e)\n", sol.J,
                sm.Pagg(), sm.qRelax(), mat.alpha);

    // Confirm both stress-specific channels are active.
    const double sumExpl = sol.termExplicit.cwiseAbs().sum();
    const double sumDju = (sol.termElastic + sol.termThermalLoad +
                           sol.termConduction).cwiseAbs().sum();
    std::printf(
        "|explicit term| sum = %.3e   |dJ/dU-propagated terms| sum = %.3e\n",
        sumExpl, sumDju);

    // Spot-check 20 random elements with central FD.
    // The forward involves two FE solves, so each J evaluation carries ~1e-13
    // relative round-off; with a 2nd-order stencil at eps=1e-6 the round-off
    // floor (~machine*|J|/eps) sits at ~1e-5 rel.err on the near-zero-gradient
    // elements. A 4th-order central stencil makes truncation negligible, letting
    // us enlarge eps to push the round-off floor far below the 1e-5 gate (eps
    // sweep: 1e-5->1.6e-6, 1e-4->1.6e-7, 1e-3->3e-8).
    std::mt19937 pick(98765u);
    std::uniform_int_distribution<int> ielem(0, g.nElems() - 1);
    const double eps = 1e-4;

    std::printf(
        "\n%4s %8s | %14s %14s | %10s | %11s %11s %11s %11s\n", "i", "elem",
        "adjoint", "FD", "rel.err", "t_explicit", "t_elastic", "t_thload",
        "t_cond");

    double maxRel = 0.0;
    for (int s = 0; s < 20; ++s) {
        const int e = ielem(pick);

        // 4th-order central stencil: truncation O(eps^4), far below the 1e-5 gate
        // even on the near-zero-gradient elements where 2nd-order would floor out.
        Vec rp1 = rho, rm1 = rho, rp2 = rho, rm2 = rho;
        rp1(e) += eps;
        rm1(e) -= eps;
        rp2(e) += 2.0 * eps;
        rm2(e) -= 2.0 * eps;
        const double Jp1 = adj.stressPNorm(rp1, sm);
        const double Jm1 = adj.stressPNorm(rm1, sm);
        const double Jp2 = adj.stressPNorm(rp2, sm);
        const double Jm2 = adj.stressPNorm(rm2, sm);
        const double fd = (-Jp2 + 8.0 * Jp1 - 8.0 * Jm1 + Jm2) / (12.0 * eps);

        const double a = sol.grad(e);
        const double rel = std::fabs(a - fd) / std::max(std::fabs(fd), 1e-300);
        maxRel = std::max(maxRel, rel);

        std::printf(
            "%4d %8d | %14.6e %14.6e | %10.3e | %11.3e %11.3e %11.3e %11.3e\n",
            s, e, a, fd, rel, sol.termExplicit(e), sol.termElastic(e),
            sol.termThermalLoad(e), sol.termConduction(e));
    }

    const bool pass = maxRel < 1e-5;
    std::printf("\nmax relative error = %.3e   -> %s\n", maxRel,
                pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
