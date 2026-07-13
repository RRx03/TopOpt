// PHASE 5 GATE (the hardest): discrete TRIPLE-coupled adjoint
//   Stokes-Brinkman(gamma) -> CHT(gamma,u) -> thermo-elastic(gamma,T)
// validated by central finite differences, entirely CPU double precision.
// PASS if max over the sampled elements of |adjoint - FD| / |FD| < 1e-3.
// The three gradient contributions (Stokes, thermal, elastic) are printed and
// must all be non-trivial (proof the triple coupling is really exercised).
#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "adjoint/TripleAdjoint.hpp"
#include "core/Grid3D.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

int main() {
    const int nel = 6;  // 6^3, unit-cube cells (h = 1)
    Grid3D g(nel, nel, nel);
    const int nN = g.nNodes();

    TripleAdjoint::Params prm;
    prm.mu = 1.0;
    prm.alphaStab = 1.0 / 12.0;
    prm.alphaMax = 1.0e2;   // moderate Brinkman (well-conditioned)
    prm.alphaMin = 0.0;
    prm.qBrink = 0.1;
    prm.ks = 1.5;           // solid conductivity (gamma=0)
    prm.kf = 0.5;           // fluid  conductivity (gamma=1)  -> dk = -1
    prm.E0 = 1.0;
    prm.Emin = 1e-4;
    prm.p = 3.0;
    prm.alphaTh = 1e-2;
    prm.Tref = 0.0;
    prm.nu = 0.3;

    const double bodyG = 20.0;  // z body force -> moderate Peclet (~1-2)
    const std::array<double, 3> bodyForce = {0.0, 0.0, bodyG};

    // --- Stokes BCs: no-slip x-walls, slip (u_y=0) on y-faces, pressure datum.
    std::vector<int> stokesFixed;
    for (int k = 0; k <= nel; ++k)
        for (int j = 0; j <= nel; ++j)
            for (int i = 0; i <= nel; ++i) {
                const int n = g.nodeId(i, j, k);
                if (i == 0 || i == nel) {
                    stokesFixed.push_back(TripleAdjoint::sdof(n, 0));
                    stokesFixed.push_back(TripleAdjoint::sdof(n, 1));
                    stokesFixed.push_back(TripleAdjoint::sdof(n, 2));
                } else if (j == 0 || j == nel) {
                    stokesFixed.push_back(TripleAdjoint::sdof(n, 1));
                }
            }
    stokesFixed.push_back(TripleAdjoint::sdof(g.nodeId(0, 0, 0), 3));

    // --- Thermal Dirichlet: T=0 at z=0 (inlet), T=1 at z=nel (outlet), Q=0.
    std::vector<std::uint8_t> dirMask(static_cast<size_t>(nN), 0);
    Vec dirVal = Vec::Zero(nN);
    for (int j = 0; j <= nel; ++j)
        for (int i = 0; i <= nel; ++i) {
            const int n0 = g.nodeId(i, j, 0);
            const int n1 = g.nodeId(i, j, nel);
            dirMask[static_cast<size_t>(n0)] = 1; dirVal(n0) = 0.0;
            dirMask[static_cast<size_t>(n1)] = 1; dirVal(n1) = 1.0;
        }
    const Vec Q = Vec::Zero(nN);

    // --- Elastic: clamp x=0 face, z-load on x=nel face (cantilever). L = Fmech.
    std::vector<int> elasticFixed;
    Vec Fmech = Vec::Zero(g.nDof());
    for (int k = 0; k <= nel; ++k)
        for (int j = 0; j <= nel; ++j) {
            const int n0 = g.nodeId(0, j, k);
            elasticFixed.push_back(3 * n0 + 0);
            elasticFixed.push_back(3 * n0 + 1);
            elasticFixed.push_back(3 * n0 + 2);
            const int n1 = g.nodeId(nel, j, k);
            Fmech(3 * n1 + 2) = -1e-2;
        }

    TripleAdjoint adj(g, prm, stokesFixed, bodyForce, dirMask, dirVal, Q,
                      elasticFixed, Fmech);

    // Random design in [0.3, 0.7] (fixed seed).
    std::mt19937 rng(12345u);
    std::uniform_real_distribution<double> dist(0.3, 0.7);
    Vec gamma(g.nElems());
    for (int e = 0; e < g.nElems(); ++e) gamma(e) = dist(rng);

    const auto sol = adj.solve(gamma);
    const double speed = adj.maxSpeed(sol.w);
    const double kMid = prm.ks + (prm.kf - prm.ks) * 0.5;
    std::printf("J = %.10e   (6^3 triple-coupled)\n", sol.J);
    std::printf("max|u| = %.4f   Pe_e ~ %.2f   T range [%.4f, %.4f]\n", speed,
                speed * 1.0 / (2.0 * kMid), sol.T.minCoeff(), sol.T.maxCoeff());

    double sS = 0, sT = 0, sE = 0;
    for (int e = 0; e < g.nElems(); ++e) {
        sS += std::fabs(sol.termStokes(e));
        sT += std::fabs(sol.termThermal(e));
        sE += std::fabs(sol.termElastic(e));
    }
    std::printf("Sum|term_stokes| = %.4e   Sum|term_thermal| = %.4e   "
                "Sum|term_elastic| = %.4e\n", sS, sT, sE);

    // Central finite differences on random elements, eps = 1e-6.
    std::mt19937 pick(98765u);
    std::uniform_int_distribution<int> ielem(0, g.nElems() - 1);
    const double eps = 1e-6;
    const int nProbe = 18;

    std::printf("\n%4s %6s | %15s %15s | %10s | %11s %11s %11s\n", "i", "elem",
                "adjoint", "FD", "rel.err", "t_stokes", "t_thermal", "t_elastic");

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
        std::printf("%4d %6d | %15.7e %15.7e | %10.3e | %11.3e %11.3e %11.3e\n",
                    s, e, a, fd, rel, sol.termStokes(e), sol.termThermal(e),
                    sol.termElastic(e));
    }

    const bool triple = sS > 1e-12 && sT > 1e-12 && sE > 1e-12;
    const bool pass = (maxRel < 1e-3) && triple;
    std::printf("\nmax rel err = %.3e   max abs err = %.3e   eps = %.0e\n",
                maxRel, maxAbs, eps);
    std::printf("triple active (all 3 terms non-trivial) = %d\n", triple);
    std::printf("-> %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
