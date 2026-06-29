// Phase 4 GATE: discrete two-block thermo-elastic adjoint validated by central
// finite differences, entirely in CPU double precision (Eigen direct solves).
// PASS if max over 20 random elements of |adjoint - FD| / |FD| < 1e-5.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "adjoint/ThermoElasticAdjoint.hpp"
#include "core/Grid3D.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

int main() {
    // --- Case: 8x8x8 cube, one-way thermo-elastic. ---
    const int nel = 8;
    Grid3D g(nel, nel, nel);

    ThermoElasticAdjoint::Material mat;  // E0=1,Emin=1e-4,p=3; k0=1,kmin=1e-4,q=3
    mat.alpha = 1e-3;
    mat.Tref = 0.0;
    mat.nu = 0.3;

    // BCs.
    // Elastic: clamp the x=0 face (all 3 DOFs) -> cantilever, well-posed.
    // Thermal: T=0 (Dirichlet) on the x=0 face.
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

    // Loads on the opposite (x=nel) face.
    //   F_mech: downward (z) shear, L = F_mech.
    //   Q: heat source -> conduction toward x=0, builds a temperature gradient.
    Vec Fmech = Vec::Zero(g.nDof());
    Vec Q = Vec::Zero(g.nNodes());
    for (int k = 0; k <= nel; ++k)
        for (int j = 0; j <= nel; ++j) {
            const int n = g.nodeId(nel, j, k);
            Fmech(3 * n + 2) = -1e-2;
            Q(n) = 5.0;
        }

    ThermoElasticAdjoint adj(g, mat, elasticFixed, thermalFixed, Fmech, Q);

    // Random density in [0.3, 0.7] (fixed seed).
    std::mt19937 rng(12345u);
    std::uniform_real_distribution<double> dist(0.3, 0.7);
    Vec rho(g.nElems());
    for (int e = 0; e < g.nElems(); ++e) rho(e) = dist(rng);

    // Adjoint gradient.
    const auto sol = adj.solve(rho);
    std::printf("J = %.10e   (8x8x8, p=q=3, alpha=%.1e)\n", sol.J, mat.alpha);
    std::printf("T range [%.4e, %.4e]   U_z(tip mean) drives J\n",
                sol.T.minCoeff(), sol.T.maxCoeff());

    // Spot-check 20 random elements with central FD, eps = 1e-6.
    std::mt19937 pick(98765u);
    std::uniform_int_distribution<int> ielem(0, g.nElems() - 1);
    const double eps = 1e-6;

    std::printf(
        "\n%4s %8s | %14s %14s | %10s | %11s %11s %11s\n", "i", "elem",
        "adjoint", "FD", "rel.err", "t_elastic", "t_thload", "t_cond");

    double maxRel = 0.0;
    for (int s = 0; s < 20; ++s) {
        const int e = ielem(pick);

        Vec rp = rho, rm = rho;
        rp(e) += eps;
        rm(e) -= eps;
        const double Jp = adj.objective(rp);
        const double Jm = adj.objective(rm);
        const double fd = (Jp - Jm) / (2.0 * eps);

        const double a = sol.grad(e);
        const double rel = std::fabs(a - fd) / std::max(std::fabs(fd), 1e-300);
        maxRel = std::max(maxRel, rel);

        std::printf("%4d %8d | %14.6e %14.6e | %10.3e | %11.3e %11.3e %11.3e\n",
                    s, e, a, fd, rel, sol.termElastic(e),
                    sol.termThermalLoad(e), sol.termConduction(e));
    }

    const bool pass = maxRel < 1e-5;
    std::printf("\nmax relative error = %.3e   -> %s\n", maxRel,
                pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
