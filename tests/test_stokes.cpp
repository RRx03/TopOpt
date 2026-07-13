// CPU Stokes validation (no Metal): Q1-Q1 + PSPG against the analytic plane
// Poiseuille channel. Two driving mechanisms, both giving u_z(x) =
// (G/2μ) x (Lx−x), u_max = G Lx²/(8μ):
//
//  (1) Body-force-driven: f_z = G, do-nothing on z-faces -> p ≡ 0. Validates
//      velocity accuracy / convergence / no-checkerboard.
//  (2) Pressure-driven: f = 0, an imposed normal traction t = −p·n on the
//      inlet/outlet z-faces (consistent boundary load ∫_Γ t·v). Δp = p_in−p_out
//      drives the flow and yields a NON-trivial pressure that must vary linearly
//      from p_in to p_out along z. This exercises the B / Bᵀ coupling and their
//      signs (a sign bug there is invisible when p ≡ 0).
//
// Common BCs: no-slip at x=0,x=Lx; symmetry/slip (u_y=0) on the y-faces.
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/Grid3D.hpp"
#include "physics/StokesSolver.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

namespace {

constexpr double kMu = 1.0;
constexpr double kLx = 1.0;
constexpr double kG = 8.0;                       // body force f_z
constexpr double kUmax = kG * kLx * kLx / (8.0 * kMu);  // = 1.0
constexpr double kAlphaStab = 1.0 / 12.0;        // PSPG coefficient (retained)

double analyticUz(double x) { return (kG / (2.0 * kMu)) * x * (kLx - x); }

struct Result {
    double nodalRelErr;   // max |u_z_fem − u_z_exact| / u_max over nodes
    double interiorRelErr;  // mid-element relative error (interpolation, O(h²))
    double maxAbsUxy;     // spurious transverse velocity
    double maxAbsP;       // pressure magnitude (exact = 0)
    double maxPjump;      // max neighbour-to-neighbour pressure jump
    bool solveOk;
};

// Build and solve the body-force-driven channel on an nelx x 2 x 2 grid.
Result runChannel(int nelx, double alphaStab) {
    const int nely = 2, nelz = 2;
    const double h = kLx / nelx;  // cube cells -> Lx = nelx*h = 1
    Grid3D g(nelx, nely, nelz);
    StokesSolver stokes(g, kMu, h, alphaStab);

    std::vector<int> fixed;
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            for (int i = 0; i <= nelx; ++i) {
                const int n = g.nodeId(i, j, k);
                if (i == 0 || i == nelx) {          // no-slip walls
                    fixed.push_back(StokesSolver::dof(n, 0));
                    fixed.push_back(StokesSolver::dof(n, 1));
                    fixed.push_back(StokesSolver::dof(n, 2));
                } else if (j == 0 || j == nely) {   // y symmetry / slip
                    fixed.push_back(StokesSolver::dof(n, 1));
                }
            }
    fixed.push_back(StokesSolver::dof(g.nodeId(0, 0, 0), 3));  // pressure datum
    stokes.setFixedDofs(fixed);

    const Vec U = stokes.solve({0.0, 0.0, kG});

    Result r{};
    r.solveOk = U.allFinite();

    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            for (int i = 0; i <= nelx; ++i) {
                const int n = g.nodeId(i, j, k);
                const double uz = U(StokesSolver::dof(n, 2));
                const double err = std::fabs(uz - analyticUz(i * h));
                r.nodalRelErr = std::max(r.nodalRelErr, err / kUmax);
                r.maxAbsUxy = std::max({r.maxAbsUxy,
                                        std::fabs(U(StokesSolver::dof(n, 0))),
                                        std::fabs(U(StokesSolver::dof(n, 1)))});
                r.maxAbsP = std::max(r.maxAbsP,
                                     std::fabs(U(StokesSolver::dof(n, 3))));
            }

    // Interior (mid-element) error along an interior y,z line: the field is
    // linear in x inside each element, so this exposes the O(h²) interpolation
    // error of the parabola (zero at nodes by 1D superconvergence).
    for (int i = 0; i < nelx; ++i) {
        const int n0 = g.nodeId(i, 1, 1);
        const int n1 = g.nodeId(i + 1, 1, 1);
        const double uzMid = 0.5 * (U(StokesSolver::dof(n0, 2)) +
                                    U(StokesSolver::dof(n1, 2)));
        const double err = std::fabs(uzMid - analyticUz((i + 0.5) * h));
        r.interiorRelErr = std::max(r.interiorRelErr, err / kUmax);
    }

    // Pressure checkerboard probe: largest jump between adjacent nodes in x.
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            for (int i = 0; i < nelx; ++i) {
                const double p0 = U(StokesSolver::dof(g.nodeId(i, j, k), 3));
                const double p1 = U(StokesSolver::dof(g.nodeId(i + 1, j, k), 3));
                r.maxPjump = std::max(r.maxPjump, std::fabs(p1 - p0));
            }
    return r;
}

bool testPoiseuille() {
    const Result r10 = runChannel(10, kAlphaStab);
    const Result r20 = runChannel(20, kAlphaStab);

    std::printf("[Poiseuille] u_max=%.4f  alpha_stab=%.4f  tau(nx=10)=%.3e\n",
                kUmax, kAlphaStab, kAlphaStab * (0.1 * 0.1) / kMu);
    std::printf("  nx=10: nodal relErr=%.3e  interior relErr=%.3e  "
                "|u_xy|=%.2e\n", r10.nodalRelErr, r10.interiorRelErr,
                r10.maxAbsUxy);
    std::printf("  nx=20: nodal relErr=%.3e  interior relErr=%.3e  "
                "|u_xy|=%.2e\n", r20.nodalRelErr, r20.interiorRelErr,
                r20.maxAbsUxy);
    std::printf("  pressure: max|p|(nx=10)=%.3e  max neighbour jump=%.3e\n",
                r10.maxAbsP, r10.maxPjump);
    std::printf("  interior error ratio (nx10/nx20)=%.2f (O(h^2) -> ~4)\n",
                r20.interiorRelErr > 0 ? r10.interiorRelErr / r20.interiorRelErr
                                       : 0.0);

    const bool accurate = r10.nodalRelErr < 1e-2 && r20.nodalRelErr < 1e-2;
    const bool converges = r20.interiorRelErr < r10.interiorRelErr;
    const bool smoothP = r10.maxAbsP < 1e-6 && r10.maxPjump < 1e-6;
    const bool clean = r10.maxAbsUxy < 1e-8 && r10.solveOk && r20.solveOk;

    std::printf("  -> accurate(<1%%)=%d  converges=%d  smoothP=%d  clean=%d\n",
                accurate, converges, smoothP, clean);
    return accurate && converges && smoothP && clean;
}

// Diagnostic: without enough stabilisation the Q1-Q1 pair violates inf-sup and
// the pressure develops a checkerboard mode (LL-LIT-002). Confirms the PSPG term
// is what keeps the pressure clean.
bool testStabilisationMatters() {
    const Result weak = runChannel(10, 1e-7);
    const Result strong = runChannel(10, kAlphaStab);
    std::printf("[inf-sup] alpha=1e-7: max|p|=%.3e jump=%.3e | "
                "alpha=%.4f: max|p|=%.3e jump=%.3e\n",
                weak.maxAbsP, weak.maxPjump, kAlphaStab, strong.maxAbsP,
                strong.maxPjump);
    // Proper stabilisation must give a far smoother pressure than the
    // near-singular case (checkerboard / blow-up when alpha ~ 0).
    const bool ok = strong.maxPjump < 1e-6 &&
                    weak.maxPjump > 1e3 * std::max(strong.maxPjump, 1e-30);
    std::printf("  -> stabilisation decisive=%d\n", ok);
    return ok;
}

struct PdResult {
    double uzRelErr;    // max |u_z − analytic| / u_max
    double uxyRel;      // max transverse velocity / u_max
    double pLinErr;     // per-z-plane mean pressure vs analytic ramp, / dP
    double pXvar;       // in-plane (x) pressure spread / dP (checkerboard probe)
    double pXosc;       // alternating (node-to-node sign-flip) component / dP
    bool solveOk;
};

// (2) Pressure-driven Poiseuille on an nelx x 2 x nelx grid (cube cells ->
// Lx = Lz = 1). No body force; flow driven by an imposed normal traction
// t = −p·n on the inlet (z=0, t_z=+p_in) and outlet (z=Lz, t_z=−p_out) faces
// (consistent boundary load ∫_Γ t·v). With Δp = p_in−p_out, dp/dz = −G, so the
// same parabolic profile arises AND pressure must ramp linearly p_in -> p_out.
// This exercises B / Bᵀ and their signs (invisible when p ≡ 0). The equal-order
// PSPG term leaves an O(h) pressure boundary layer near the z-walls, so the
// pressure is validated by convergence, not by exactness.
PdResult runPressureDriven(int nelx) {
    const int nely = 2, nelz = nelx;   // cube cells -> Lz = nelz*h = Lx = 1
    const double h = kLx / nelx;
    const double Lz = nelz * h;
    const double G = kG;                 // keep u_max = 1
    const double dP = G * Lz;            // p_in − p_out
    const double pIn = dP, pOut = 0.0;   // p(z) = dP − G z

    Grid3D g(nelx, nely, nelz);
    StokesSolver stokes(g, kMu, h, kAlphaStab);

    std::vector<int> fixed;
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            for (int i = 0; i <= nelx; ++i) {
                const int n = g.nodeId(i, j, k);
                if (i == 0 || i == nelx) {          // no-slip walls
                    fixed.push_back(StokesSolver::dof(n, 0));
                    fixed.push_back(StokesSolver::dof(n, 1));
                    fixed.push_back(StokesSolver::dof(n, 2));
                } else if (j == 0 || j == nely) {   // y symmetry / slip
                    fixed.push_back(StokesSolver::dof(n, 1));
                }
            }
    // No pressure datum: the inlet/outlet traction sets the pressure level.
    stokes.setFixedDofs(fixed);

    // Consistent boundary-traction load ∫_Γ t·v on z-faces. Flat square face
    // of side h, bilinear shapes -> h²/4 per corner node, accumulated.
    Vec load = Vec::Zero(stokes.nDofTotal());
    const double w = h * h / 4.0;
    for (int ey = 0; ey < nely; ++ey)
        for (int ex = 0; ex < nelx; ++ex)
            for (int dj = 0; dj < 2; ++dj)
                for (int di = 0; di < 2; ++di) {
                    const int nIn = g.nodeId(ex + di, ey + dj, 0);
                    const int nOut = g.nodeId(ex + di, ey + dj, nelz);
                    load(StokesSolver::dof(nIn, 2)) += pIn * w;   // t_z = +p_in
                    load(StokesSolver::dof(nOut, 2)) += -pOut * w;  // t_z = −p_out
                }

    const Vec U = stokes.solve({0.0, 0.0, 0.0}, load);

    PdResult r{};
    r.solveOk = U.allFinite();
    for (int k = 0; k <= nelz; ++k)
        for (int j = 0; j <= nely; ++j)
            for (int i = 0; i <= nelx; ++i) {
                const int n = g.nodeId(i, j, k);
                r.uzRelErr = std::max(r.uzRelErr,
                    std::fabs(U(StokesSolver::dof(n, 2)) - analyticUz(i * h)) /
                    kUmax);
                r.uxyRel = std::max({r.uxyRel,
                    std::fabs(U(StokesSolver::dof(n, 0))) / kUmax,
                    std::fabs(U(StokesSolver::dof(n, 1))) / kUmax});
            }

    // Pressure linearity in z: per-plane mean vs the analytic ramp.
    for (int k = 0; k <= nelz; ++k) {
        double sum = 0.0;
        int cnt = 0;
        double pmin = 1e300, pmax = -1e300;
        for (int j = 0; j <= nely; ++j)
            for (int i = 0; i <= nelx; ++i) {
                const double p = U(StokesSolver::dof(g.nodeId(i, j, k), 3));
                sum += p;
                ++cnt;
                pmin = std::min(pmin, p);
                pmax = std::max(pmax, p);
            }
        const double pExact = dP - G * (k * h);
        r.pLinErr = std::max(r.pLinErr, std::fabs(sum / cnt - pExact) / dP);
        r.pXvar = std::max(r.pXvar, (pmax - pmin) / dP);  // in-plane spread
        // Alternating component along x at this plane (checkerboard signature):
        // a true damier flips sign every node; a smooth gradient does not.
        const int j = nely / 2;
        for (int i = 1; i < nelx; ++i) {
            const double pm = U(StokesSolver::dof(g.nodeId(i - 1, j, k), 3));
            const double p0 = U(StokesSolver::dof(g.nodeId(i, j, k), 3));
            const double pp = U(StokesSolver::dof(g.nodeId(i + 1, j, k), 3));
            r.pXosc = std::max(r.pXosc, std::fabs(p0 - 0.5 * (pm + pp)) / dP);
        }
    }
    return r;
}

bool testPressureDriven() {
    const PdResult r10 = runPressureDriven(10);
    const PdResult r20 = runPressureDriven(20);

    std::printf("[pressure-driven] dP=%.1f (Lx=Lz=1), traction-driven, no f\n",
                kG * 1.0);
    std::printf("  nx=10: u_z relErr=%.3e  |u_xy|/umax=%.2e  p z-lin err=%.3e  "
                "p x-spread=%.2e  p x-osc=%.2e\n", r10.uzRelErr, r10.uxyRel,
                r10.pLinErr, r10.pXvar, r10.pXosc);
    std::printf("  nx=20: u_z relErr=%.3e  |u_xy|/umax=%.2e  p z-lin err=%.3e  "
                "p x-spread=%.2e  p x-osc=%.2e\n", r20.uzRelErr, r20.uxyRel,
                r20.pLinErr, r20.pXvar, r20.pXosc);

    const bool accurate = r10.uzRelErr < 1e-2 && r20.uzRelErr < 1e-2;
    const bool uConv = r20.uzRelErr < r10.uzRelErr;
    const bool pLinear = r20.pLinErr < 2.5e-2 && r20.pLinErr < r10.pLinErr;
    // Smooth (no checkerboard): the alternating x-component is small AND shrinks
    // with h (a real damier would be O(1) and non-convergent).
    const bool smoothP = r20.pXosc < 1e-2 && r20.pXosc < 0.75 * r10.pXosc;
    const bool clean = r10.uxyRel < 1e-2 && r20.uxyRel < r10.uxyRel &&
                       r10.solveOk && r20.solveOk;
    std::printf("  -> accurate(<1%%)=%d  uConv=%d  pLinear(<2.5%%)=%d  "
                "smoothP=%d  clean=%d\n", accurate, uConv, pLinear, smoothP,
                clean);
    return accurate && uConv && pLinear && smoothP && clean;
}

} // namespace

int main() {
    int fails = 0;
    fails += testPoiseuille() ? 0 : 1;
    fails += testPressureDriven() ? 0 : 1;
    fails += testStabilisationMatters() ? 0 : 1;
    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    return fails;
}
