// Stress model validation (CPU double). Uniaxial tension: von Mises equals the
// applied uniaxial stress. p-norm aggregation approaches the max element stress
// as the aggregation exponent grows.
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Grid3D.hpp"
#include "fem/FEM3D.hpp"
#include "topopt/StressModel.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

int main() {
    const int nelx = 6, nely = 4, nelz = 4;
    const double nu = 0.3;
    Grid3D g(nelx, nely, nelz);
    FEM3D fem(g, nu);

    // Symmetry BCs (uniaxial tension along x), like the FEM patch test.
    std::vector<int> fixed;
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j) fixed.push_back(3 * g.nodeId(0, j, k) + 0);
    for (int k = 0; k <= nelz; ++k)
        for (int i = 0; i <= nelx; ++i) fixed.push_back(3 * g.nodeId(i, 0, k) + 1);
    for (int j = 0; j <= nely; ++j)
        for (int i = 0; i <= nelx; ++i) fixed.push_back(3 * g.nodeId(i, j, 0) + 2);
    fem.setFixedDofs(fixed);

    const double sigmaApplied = 0.5;  // uniaxial stress to apply on x=L face
    const double A = static_cast<double>(nely * nelz);
    const double Ftot = sigmaApplied * A;
    Vec F = Vec::Zero(g.nDof());
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j) {
            const double wj = (j == 0 || j == nely) ? 0.5 : 1.0;
            const double wk = (k == 0 || k == nelz) ? 0.5 : 1.0;
            F(3 * g.nodeId(nelx, j, k) + 0) = Ftot * wj * wk / A;
        }

    const Vec E = Vec::Constant(g.nElems(), 1.0);
    const Vec U = fem.solve(E, F);

    StressModel sm(nu, /*qRelax=*/0.5, /*Pagg=*/8.0);
    const Vec vm0 = sm.vonMisesSolid(g, U);

    int fails = 0;

    // (a) uniaxial: von Mises == applied stress everywhere.
    double maxerr = 0.0;
    for (int e = 0; e < g.nElems(); ++e)
        maxerr = std::max(maxerr, std::fabs(vm0(e) - sigmaApplied));
    std::printf("[a] uniaxial von Mises: applied=%.4f maxerr=%.2e\n",
                sigmaApplied, maxerr);
    fails += (maxerr < 1e-6) ? 0 : 1;

    // (b) p-norm bounds: max <= p-norm and p-norm within a few % of max for P=8,
    //     converging to max as P grows.
    const Vec rho = Vec::Constant(g.nElems(), 1.0);  // q-relax with rho=1 -> sigma=vm0
    const Vec sigma = sm.relaxedStress(rho, vm0);
    const double smax = sigma.maxCoeff();
    const double pn8 = sm.pNorm(sigma);
    StressModel sm16(nu, 0.5, 16.0);
    const double pn16 = sm16.pNorm(sigma);
    std::printf("[b] p-norm: max=%.4f  PN(8)=%.4f  PN(16)=%.4f\n", smax, pn8, pn16);
    fails += (pn8 >= smax - 1e-9 && pn16 >= smax - 1e-9 &&
              pn16 <= pn8 + 1e-9 && (pn16 - smax) < (pn8 - smax) + 1e-12) ? 0 : 1;

    // (c) relaxation: sigma_e = rho^q * vm0 vanishes as rho->0.
    const Vec rhoVoid = Vec::Constant(g.nElems(), 1e-6);
    const double sVoid = sm.relaxedStress(rhoVoid, vm0).maxCoeff();
    std::printf("[c] relaxation rho=1e-6: max relaxed stress=%.3e (-> 0)\n", sVoid);
    fails += (sVoid < 0.1 * sigmaApplied) ? 0 : 1;

    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    return fails;
}
