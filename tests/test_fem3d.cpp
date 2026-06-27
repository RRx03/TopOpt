// CPU FEM 3D validation (no Metal): element properties, constant-strain patch
// test (exact), and a cantilever bending sanity check. Each test prints and
// returns true on success.
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Grid3D.hpp"
#include "fem/FEM3D.hpp"
#include "fem/H8Element.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

namespace {

// (a) KE0 sanity: symmetry and rigid-body translation in the null space.
bool testElementProperties() {
    const auto KE = H8Element::stiffness(0.3);
    double asym = (KE - KE.transpose()).cwiseAbs().maxCoeff();

    // Rigid translation in x: u = (1,0,0) repeated -> KE u = 0.
    Eigen::Matrix<double, 24, 1> tx = Eigen::Matrix<double, 24, 1>::Zero();
    for (int a = 0; a < 8; ++a) tx(3 * a) = 1.0;
    double rbx = (KE * tx).cwiseAbs().maxCoeff();

    std::printf("[a] H8 KE0: asym=%.2e  rigid-x=%.2e\n", asym, rbx);
    return asym < 1e-12 && rbx < 1e-10;
}

// (b) Constant-strain patch test via uniaxial tension with symmetry BCs.
// Linear displacement field is exact in the trilinear space -> u_x(x=L) = F*L/(E*A).
bool testTensionPatch() {
    const int nelx = 4, nely = 3, nelz = 2;
    Grid3D g(nelx, nely, nelz);
    FEM3D fem(g, 0.3);

    std::vector<int> fixed;
    // x=0 plane: ux=0 ; y=0 plane: uy=0 ; z=0 plane: uz=0  (symmetry).
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            fixed.push_back(3 * g.nodeId(0, j, k) + 0);
    for (int k = 0; k <= nelz; ++k)
        for (int i = 0; i <= nelx; ++i)
            fixed.push_back(3 * g.nodeId(i, 0, k) + 1);
    for (int j = 0; j <= nely; ++j)
        for (int i = 0; i <= nelx; ++i)
            fixed.push_back(3 * g.nodeId(i, j, 0) + 2);
    fem.setFixedDofs(fixed);

    // Uniform traction sigma on x=L face -> consistent nodal forces.
    const double Ftot = 1.0;
    const double A = static_cast<double>(nely * nelz);
    const double sigma = Ftot / A;
    Vec F = Vec::Zero(g.nDof());
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j) {
            const double wj = (j == 0 || j == nely) ? 0.5 : 1.0;
            const double wk = (k == 0 || k == nelz) ? 0.5 : 1.0;
            F(3 * g.nodeId(nelx, j, k) + 0) = sigma * wj * wk;
        }

    const Vec E = Vec::Constant(g.nElems(), 1.0);
    const Vec U = fem.solve(E, F);

    const double expected = Ftot * nelx / (1.0 * A);  // F*L/(E*A), E=1
    double maxerr = 0.0;
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            maxerr = std::max(maxerr,
                std::fabs(U(3 * g.nodeId(nelx, j, k) + 0) - expected));
    std::printf("[b] tension patch: expected u_x=%.6f maxerr=%.2e\n",
                expected, maxerr);
    return maxerr < 1e-10;
}

// (c) Cantilever bending sanity: clamp x=0 face, tip shear load -> deflection
// in the Euler-Bernoulli ballpark (coarse mesh + shear -> loose tolerance).
bool testCantilever() {
    const int nelx = 20, nely = 4, nelz = 4;
    Grid3D g(nelx, nely, nelz);
    const double Emod = 1.0, nu = 0.3;
    FEM3D fem(g, nu);

    std::vector<int> fixed;
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j) {
            const int n = g.nodeId(0, j, k);
            fixed.push_back(3 * n + 0);
            fixed.push_back(3 * n + 1);
            fixed.push_back(3 * n + 2);
        }
    fem.setFixedDofs(fixed);

    const double Ftot = -1e-3;  // downward (z) at the free end
    Vec F = Vec::Zero(g.nDof());
    const int ntip = (nely + 1) * (nelz + 1);
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            F(3 * g.nodeId(nelx, j, k) + 2) = Ftot / ntip;

    const Vec Evec = Vec::Constant(g.nElems(), Emod);
    const Vec U = fem.solve(Evec, F);

    double wtip = 0.0;
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            wtip += U(3 * g.nodeId(nelx, j, k) + 2);
    wtip /= ntip;

    const double L = nelx, b = nely, h = nelz;
    const double I = b * h * h * h / 12.0;
    const double wEB = Ftot * L * L * L / (3.0 * Emod * I);
    const double ratio = wtip / wEB;
    std::printf("[c] cantilever: w_fem=%.4e  w_EB=%.4e  ratio=%.3f\n",
                wtip, wEB, ratio);
    // FE (with shear) is more compliant than slender EB: ratio >= 1, within ~30%.
    return ratio > 0.9 && ratio < 1.4;
}

} // namespace

int main() {
    int fails = 0;
    fails += testElementProperties() ? 0 : 1;
    fails += testTensionPatch() ? 0 : 1;
    fails += testCantilever() ? 0 : 1;
    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    return fails;
}
