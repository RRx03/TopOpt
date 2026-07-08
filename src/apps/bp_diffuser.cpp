// PHASE 5R: reproduce the canonical Borrvall-Petersson (2003) fluid-topology
// benchmark -- the DIFFUSER -- with the FD-validated dissipated-power gradient
// (DissipationAdjoint, gate DF 7e-7) + MMA + 3D density filter. CPU double.
//
// Problem (BP 2003, Fig. of the "diffuser"):
//   Square quasi-2D slab [0,nx]x[0,ny]x[0,1] (unit cells, h = 1). A parabolic
//   inlet spreads over the FULL left edge; the outlet is a REDUCED central
//   opening (~1/3 height) on the right edge, the rest of the right edge and the
//   top/bottom walls are no-slip. Slip (u_z = 0) on the two z-faces makes the
//   flow planar. Minimise the viscous+Brinkman dissipation
//       Phi(gamma,u) = 1/2 integral mu|grad u|^2 + 1/2 integral alpha(gamma)|u|^2
//   subject to a fluid-fraction budget mean(gamma) <= V. gamma = 1 fluid,
//   gamma = 0 solid. The published optimum is a SMOOTH CONVERGENT CHANNEL from
//   the wide inlet to the narrow outlet (no spurious bend, no checkerboard).
//
// Pipeline per MMA iteration:
//   rho --filter W--> rho_tilde --Heaviside(beta)--> gamma
//   gamma --DissipationAdjoint::solve--> Phi, dPhi/dgamma
//   dPhi/drho = W^T ( dHeaviside .* dPhi/dgamma )
// Brinkman continuation alpha_max 1e2 -> 1e4 (LL-LIT-004: avoid local minima and
// keep K well-conditioned); the adjoint operator is rebuilt when alpha_max steps.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "adjoint/DissipationAdjoint.hpp"
#include "core/Grid3D.hpp"
#include "io/VTKExporter.hpp"
#include "topopt/MMAOptimizer.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;
using Mat = Eigen::MatrixXd;

namespace {

// 3D density filter (linear hat, radius rmin cells), rows normalised to sum 1 so
// applyT is the exact transpose used to chain sensitivities (from cooling_jacket).
struct DensityFilter3D {
    const Grid3D& g;
    double rmin;
    std::vector<std::vector<std::pair<int, double>>> wts;

    DensityFilter3D(const Grid3D& grid, double r) : g(grid), rmin(r) {
        const int nx = g.nelx(), ny = g.nely(), nz = g.nelz();
        wts.resize(static_cast<size_t>(nx * ny * nz));
        const int R = static_cast<int>(std::ceil(rmin));
        for (int ez = 0; ez < nz; ++ez)
            for (int ey = 0; ey < ny; ++ey)
                for (int ex = 0; ex < nx; ++ex) {
                    const int e = g.elemId(ex, ey, ez);
                    double wsum = 0.0;
                    for (int dz = -R; dz <= R; ++dz)
                        for (int dy = -R; dy <= R; ++dy)
                            for (int dx = -R; dx <= R; ++dx) {
                                const int ni = ex + dx, nj = ey + dy, nk = ez + dz;
                                if (ni < 0 || ni >= nx || nj < 0 || nj >= ny ||
                                    nk < 0 || nk >= nz)
                                    continue;
                                const double dist =
                                    std::sqrt(double(dx * dx + dy * dy + dz * dz));
                                const double w = rmin - dist;
                                if (w <= 0.0) continue;
                                wts[static_cast<size_t>(e)].push_back(
                                    {g.elemId(ni, nj, nk), w});
                                wsum += w;
                            }
                    for (auto& pr : wts[static_cast<size_t>(e)]) pr.second /= wsum;
                }
    }
    Vec apply(const Vec& x) const {
        Vec y = Vec::Zero(x.size());
        for (size_t e = 0; e < wts.size(); ++e)
            for (const auto& pr : wts[e])
                y(static_cast<Eigen::Index>(e)) += pr.second * x(pr.first);
        return y;
    }
    Vec applyT(const Vec& gin) const {
        Vec out = Vec::Zero(gin.size());
        for (size_t e = 0; e < wts.size(); ++e)
            for (const auto& pr : wts[e])
                out(pr.first) += pr.second * gin(static_cast<Eigen::Index>(e));
        return out;
    }
};

// Heaviside projection (Wang-Lazarov-Sigmund), eta = 0.5; input clamped to [0,1]
// (LL-008: never feed tanh/pow a value outside the design box).
double heaviside(double rt, double beta, double eta) {
    const double r = std::clamp(rt, 0.0, 1.0);
    const double tb = std::tanh(beta * eta);
    return (tb + std::tanh(beta * (r - eta))) /
           (tb + std::tanh(beta * (1.0 - eta)));
}
double dHeaviside(double rt, double beta, double eta) {
    const double r = std::clamp(rt, 0.0, 1.0);
    const double tb = std::tanh(beta * eta);
    const double s = std::tanh(beta * (r - eta));
    return beta * (1.0 - s * s) / (tb + std::tanh(beta * (1.0 - eta)));
}

// Enforce mid-height (y) symmetry of a per-element field: ey <-> nely-1-ey. The
// diffuser problem is symmetric about y = ny/2 (parabolic inlet, centered outlet,
// symmetric walls); symmetrising the design removes the spurious symmetry-broken
// local minima that MMA otherwise drifts into from numerical noise.
void symmetrizeY(const Grid3D& g, Vec& x) {
    const int nx = g.nelx(), ny = g.nely(), nz = g.nelz();
    for (int ez = 0; ez < nz; ++ez)
        for (int ey = 0; ey < ny / 2; ++ey)
            for (int ex = 0; ex < nx; ++ex) {
                const int a = g.elemId(ex, ey, ez);
                const int b = g.elemId(ex, ny - 1 - ey, ez);
                const double m = 0.5 * (x(a) + x(b));
                x(a) = m; x(b) = m;
            }
}

// Average a nodal scalar over an element's 8 corners -> cell value.
Vec nodalScalarToCell(const Grid3D& g, const Vec& nodal) {
    Vec c = Vec::Zero(g.nElems());
    for (int ez = 0; ez < g.nelz(); ++ez)
        for (int ey = 0; ey < g.nely(); ++ey)
            for (int ex = 0; ex < g.nelx(); ++ex) {
                const auto n = g.elementNodes(ex, ey, ez);
                double s = 0.0;
                for (int a = 0; a < 8; ++a) s += nodal(n[static_cast<size_t>(a)]);
                c(g.elemId(ex, ey, ez)) = s / 8.0;
            }
    return c;
}

// ---- minimal self-contained PNG writer (RGB8, uncompressed zlib blocks). ----
std::uint32_t crc32(const std::vector<std::uint8_t>& buf) {
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        init = true;
    }
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::uint8_t b : buf) c = table[(c ^ b) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
void putBE(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(std::uint8_t(x >> 24)); v.push_back(std::uint8_t(x >> 16));
    v.push_back(std::uint8_t(x >> 8));  v.push_back(std::uint8_t(x));
}
void writeChunk(std::ofstream& f, const char* type,
                const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> len;
    putBE(len, std::uint32_t(data.size()));
    f.write(reinterpret_cast<const char*>(len.data()), 4);
    std::vector<std::uint8_t> tc(type, type + 4);
    tc.insert(tc.end(), data.begin(), data.end());
    f.write(reinterpret_cast<const char*>(tc.data()),
            static_cast<std::streamsize>(tc.size()));
    std::vector<std::uint8_t> crc;
    putBE(crc, crc32(tc));
    f.write(reinterpret_cast<const char*>(crc.data()), 4);
}
bool writePNG(const std::string& path, int W, int H,
              const std::vector<std::uint8_t>& rgb) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "writePNG: cannot open %s\n", path.c_str()); return false; }
    const std::uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    f.write(reinterpret_cast<const char*>(sig), 8);
    std::vector<std::uint8_t> ihdr;
    putBE(ihdr, std::uint32_t(W)); putBE(ihdr, std::uint32_t(H));
    ihdr.push_back(8); ihdr.push_back(2);        // 8-bit, RGB
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    writeChunk(f, "IHDR", ihdr);
    // raw = per scanline: filter byte 0 + RGB row.
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<size_t>(H) * (1 + 3 * static_cast<size_t>(W)));
    for (int y = 0; y < H; ++y) {
        raw.push_back(0);
        const size_t off = static_cast<size_t>(y) * 3 * static_cast<size_t>(W);
        raw.insert(raw.end(), rgb.begin() + static_cast<long>(off),
                   rgb.begin() + static_cast<long>(off + 3 * static_cast<size_t>(W)));
    }
    // zlib stream with stored (uncompressed) deflate blocks.
    std::vector<std::uint8_t> zl; zl.push_back(0x78); zl.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = (pos + n >= raw.size());
        zl.push_back(last ? 1 : 0);
        zl.push_back(std::uint8_t(n & 0xFF));       zl.push_back(std::uint8_t((n >> 8) & 0xFF));
        zl.push_back(std::uint8_t(~n & 0xFF));      zl.push_back(std::uint8_t((~n >> 8) & 0xFF));
        zl.insert(zl.end(), raw.begin() + static_cast<long>(pos),
                  raw.begin() + static_cast<long>(pos + n));
        pos += n;
    }
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
    putBE(zl, (b << 16) | a);
    writeChunk(f, "IDAT", zl);
    writeChunk(f, "IEND", {});
    std::printf("PNG: %dx%d -> %s\n", W, H, path.c_str());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();

    // Final Brinkman alpha_max. Default 50: the sweet spot where the PSPG Q1-Q1
    // solver still conserves mass (inlet flux == mid-domain flux), so the design
    // stays a CONNECTED channel. Above ~100 the solver leaks mass across the
    // Brinkman jump and MMA islands the flow (see report). Overridable via argv.
    const double amaxFinal = (argc > 1) ? std::atof(argv[1]) : 50.0;

    // ---- geometry: square quasi-2D slab, flow along +x. ----
    const int nx = 40, ny = 40, nz = 1;
    Grid3D g(nx, ny, nz);
    const int nN = g.nNodes();
    const int ne = g.nElems();
    std::printf("bp_diffuser: grid %dx%dx%d  (%d elems, Stokes DOF=%d)\n", nx, ny,
                nz, ne, 4 * nN);

    // ---- physics parameters. ----
    DissipationAdjoint::Params prm;
    prm.mu = 1.0;
    prm.alphaStab = 1.0 / 12.0;
    prm.alphaMin = 0.0;
    prm.qBrink = 0.1;
    // alphaMax set per continuation stage below.

    const std::array<double, 3> bodyForce = {0.0, 0.0, 0.0};  // driven by inlet

    // ---- boundary conditions (diffuser). ----
    // z-faces: slip u_z = 0 (planar flow). Walls y=0,ny: no-slip. Inlet x=0:
    // parabolic u_x over full height. Outlet x=nx: central 1/3 opening, parabolic
    // u_x imposed and flux-matched to the inlet (the narrow outlet accelerates the
    // flow), rest of the right edge no-slip.
    //
    // NOTE: a FREE (zero-traction) outlet was tried first and FAILED -- the PSPG
    // Q1-Q1 solver does not strictly conserve mass across a strong-Brinkman plug,
    // so MMA islands the design (inlet flux 26.7 vs mid-domain flux ~0) to hide the
    // flow and cheat the objective. Imposing the outlet velocity (as BP do) forces
    // a genuinely connected channel and removes the cheat.
    const double Umax = 1.0;
    const int jlo = ny / 3, jhi = ny - ny / 3;  // outlet opening node band
    const int Ho = jhi - jlo;                    // opening height (cells)

    // Discrete inlet flux and outlet-profile scale so the two match exactly.
    double qInlet = 0.0;
    for (int j = 0; j <= ny; ++j) {
        const double yy = static_cast<double>(j) / ny;
        qInlet += 4.0 * Umax * yy * (1.0 - yy);
    }
    double sOut = 0.0;
    for (int j = jlo; j <= jhi; ++j) {
        const double s = static_cast<double>(j - jlo) / Ho;
        sOut += 4.0 * s * (1.0 - s);
    }
    const double Uout = qInlet / sOut;  // outlet peak velocity (accelerated)

    std::vector<int> fixed;
    Vec dirVal = Vec::Zero(4 * nN);
    for (int k = 0; k <= nz; ++k)
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i) {
                const int n = g.nodeId(i, j, k);
                if (k == 0 || k == nz)
                    fixed.push_back(DissipationAdjoint::sdof(n, 2));  // slip
                const bool wall = (j == 0 || j == ny);
                const bool inlet = (i == 0);
                const bool outlet = (i == nx);
                const bool openOutlet = outlet && (j >= jlo && j <= jhi);
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
                } else if (openOutlet) {
                    const double s = static_cast<double>(j - jlo) / Ho;
                    const double ux = Uout * 4.0 * s * (1.0 - s);
                    fixed.push_back(DissipationAdjoint::sdof(n, 0));
                    fixed.push_back(DissipationAdjoint::sdof(n, 1));
                    fixed.push_back(DissipationAdjoint::sdof(n, 2));
                    dirVal(DissipationAdjoint::sdof(n, 0)) = ux;
                } else if (outlet) {
                    fixed.push_back(DissipationAdjoint::sdof(n, 0));
                    fixed.push_back(DissipationAdjoint::sdof(n, 1));
                    fixed.push_back(DissipationAdjoint::sdof(n, 2));
                }
            }
    fixed.push_back(DissipationAdjoint::sdof(g.nodeId(0, 0, 0), 3));  // p datum

    // ---- filter + Heaviside + MMA. ----
    DensityFilter3D filt(g, /*rmin=*/1.6);
    const double eta = 0.5, vFrac = 0.5;

    MMAOptimizer::Params mp;
    mp.move = 0.2;
    MMAOptimizer mma(ne, 1, mp);
    const Vec xmin = Vec::Constant(ne, 0.0), xmax = Vec::Ones(ne);
    Vec rho = Vec::Constant(ne, vFrac);

    const int nIter = 80;
    // Brinkman continuation within the mass-conserving regime: ramp to amaxFinal.
    auto alphaMaxAt = [amaxFinal](int it) -> double {
        if (it <= 20) return 0.4 * amaxFinal;
        if (it <= 40) return 0.6 * amaxFinal;
        if (it <= 60) return 0.8 * amaxFinal;
        return amaxFinal;
    };
    auto betaAt = [](int it) -> double {
        if (it <= 20) return 1.0;
        if (it <= 40) return 2.0;
        if (it <= 60) return 4.0;
        return 8.0;
    };

    // Build the adjoint operator; rebuilt whenever alpha_max steps.
    double curAmax = -1.0;
    std::unique_ptr<DissipationAdjoint> adj;
    auto ensureAdj = [&](double amax) {
        if (amax != curAmax) {
            prm.alphaMax = amax;
            adj = std::make_unique<DissipationAdjoint>(g, prm, fixed, bodyForce, dirVal);
            curAmax = amax;
        }
    };

    std::vector<double> phiHist;
    double Phiref = 0.0, Phi = 0.0, fluidFrac = 0.0, gVol = 0.0, gray = 0.0;
    double Phi_first = 0.0;

    std::printf("%4s %10s %10s %8s %6s %8s %7s %8s\n", "it", "Phi", "Phi/ref",
                "amax", "beta", "fluid", "gVol", "gray");
    for (int it = 1; it <= nIter; ++it) {
        const double beta = betaAt(it);
        ensureAdj(alphaMaxAt(it));

        const Vec rhoTil = filt.apply(rho);
        Vec gamma(ne), dHdT(ne);
        for (int e = 0; e < ne; ++e) {
            gamma(e) = heaviside(rhoTil(e), beta, eta);
            dHdT(e) = dHeaviside(rhoTil(e), beta, eta);
        }

        const auto sol = adj->solve(gamma);
        Phi = sol.Phi;
        if (it == 1) { Phiref = std::fabs(Phi) > 1e-30 ? std::fabs(Phi) : 1.0; Phi_first = Phi; }
        phiHist.push_back(Phi);

        Vec dPdT(ne);
        for (int e = 0; e < ne; ++e) dPdT(e) = sol.grad(e) * dHdT(e);
        const Vec df0 = filt.applyT(dPdT) / Phiref;

        fluidFrac = gamma.mean();
        gVol = fluidFrac / vFrac - 1.0;
        Vec dVdT(ne);
        for (int e = 0; e < ne; ++e) dVdT(e) = (1.0 / ne) * dHdT(e) / vFrac;
        const Vec dgVol = filt.applyT(dVdT);

        Vec fvals(1); fvals(0) = gVol;
        Mat dfdx(1, ne); dfdx.row(0) = dgVol.transpose();

        rho = mma.step(rho, Phi / Phiref, df0, fvals, dfdx, xmin, xmax);
        symmetrizeY(g, rho);

        int ng = 0;
        for (int e = 0; e < ne; ++e)
            if (gamma(e) > 0.1 && gamma(e) < 0.9) ++ng;
        gray = double(ng) / ne;

        if (it == 1 || it % 5 == 0 || it == nIter)
            std::printf("%4d %10.4e %10.4f %8.0f %6.0f %8.4f %+7.4f %8.3f\n", it,
                        Phi, Phi / Phiref, alphaMaxAt(it), beta, fluidFrac, gVol, gray);
    }

    // ---- final design + fields at final continuation stage. ----
    const double betaF = betaAt(nIter);
    ensureAdj(alphaMaxAt(nIter));
    const Vec rhoTil = filt.apply(rho);
    Vec gamma(ne);
    for (int e = 0; e < ne; ++e) gamma(e) = heaviside(rhoTil(e), betaF, eta);
    const auto sol = adj->solve(gamma);
    const double Phi_opt = sol.Phi;

    // Reference: uniform design gamma = V at the same Brinkman.
    Vec gUniform = Vec::Constant(ne, vFrac);
    const double Phi_uniform = adj->objective(gUniform);

    // ---- cell fields for ParaView: density, speed, pressure. ----
    Vec speed = Vec::Zero(nN), pres = Vec::Zero(nN);
    for (int n = 0; n < nN; ++n) {
        const double ux = sol.w(4 * n + 0), uy = sol.w(4 * n + 1),
                     uz = sol.w(4 * n + 2);
        speed(n) = std::sqrt(ux * ux + uy * uy + uz * uz);
        pres(n) = sol.w(4 * n + 3);
    }
    const Vec speedC = nodalScalarToCell(g, speed);
    const Vec presC = nodalScalarToCell(g, pres);
    VTKExporter::writeImageData("output/bp_diffuser.vti", g,
                                {{"density", &gamma},
                                 {"speed", &speedC},
                                 {"pressure", &presC}});

    // ---- PNG cross-section of the density (z-slice, y up). ----
    {
        const int S = 12;                         // pixels per cell
        const int W = nx * S, H = ny * S;
        std::vector<std::uint8_t> img(static_cast<size_t>(W) * static_cast<size_t>(H) * 3);
        for (int py = 0; py < H; ++py)
            for (int px = 0; px < W; ++px) {
                const int ex = px / S;
                const int ey = (H - 1 - py) / S;  // flip so y=0 at bottom
                const double gv = std::clamp(gamma(g.elemId(ex, ey, 0)), 0.0, 1.0);
                // solid (0) = dark navy, fluid (1) = light cyan (channel bright).
                const std::uint8_t r = std::uint8_t(20 + gv * (235 - 20));
                const std::uint8_t gg = std::uint8_t(30 + gv * (245 - 30));
                const std::uint8_t bb = std::uint8_t(60 + gv * (255 - 60));
                const size_t o = (static_cast<size_t>(py) * static_cast<size_t>(W) +
                                  static_cast<size_t>(px)) * 3;
                img[o] = r; img[o + 1] = gg; img[o + 2] = bb;
            }
        writePNG("output/bp_diffuser.png", W, H, img);
    }

    // ---- summary. ----
    fluidFrac = gamma.mean();
    int ngray = 0;
    for (int e = 0; e < ne; ++e)
        if (gamma(e) > 0.1 && gamma(e) < 0.9) ++ngray;
    gray = double(ngray) / ne;

    // crude channel signature: mean fluid width per x-column at mid rows.
    auto colFluidFrac = [&](int ex) {
        double s = 0.0;
        for (int ey = 0; ey < ny; ++ey) s += gamma(g.elemId(ex, ey, 0));
        return s / ny;
    };
    const double wIn = colFluidFrac(0), wOut = colFluidFrac(nx - 1);

    double umax = 0.0;
    for (int n = 0; n < nN; ++n) umax = std::max(umax, speed(n));

    // Flux through vertical node-columns (u_x integrated over y at k=0): checks
    // mass conservation (inlet == outlet) and reveals a stagnant/islanded design.
    auto fluxAt = [&](int i) {
        double q = 0.0;
        for (int j = 0; j <= ny; ++j) q += sol.w(4 * g.nodeId(i, j, 0) + 0);
        return q;
    };
    const double qIn = fluxAt(0), qMid = fluxAt(nx / 2), qOut = fluxAt(nx);
    std::printf("flux u_x: inlet=%.3f  mid=%.3f  outlet=%.3f  (mass conserved if ~equal)\n",
                qIn, qMid, qOut);

    // ASCII map of the design (y up, x right): '#' solid, '.' fluid, ':' grey.
    std::printf("\ndensity map (%dx%d, '#'=solid '.'=fluid ':'=grey):\n", nx, ny);
    for (int ey = ny - 1; ey >= 0; --ey) {
        std::printf("  ");
        for (int ex = 0; ex < nx; ++ex) {
            const double gv = gamma(g.elemId(ex, ey, 0));
            std::putchar(gv > 0.7 ? '.' : (gv < 0.3 ? '#' : ':'));
        }
        std::putchar('\n');
    }

    const double secs = std::chrono::duration<double>(clk::now() - t0).count();
    std::printf("\n==== bp_diffuser summary ====\n");
    std::printf("Phi: first=%.4e  final=%.4e  (ratio %.3f)\n", Phi_first, Phi_opt,
                Phi_opt / (std::fabs(Phi_first) > 1e-30 ? Phi_first : 1.0));
    std::printf("Phi_opt=%.4e  vs  Phi_uniform(gamma=V)=%.4e  -> improvement %.2f%% (%s)\n",
                Phi_opt, Phi_uniform, 100.0 * (Phi_uniform - Phi_opt) / Phi_uniform,
                Phi_opt < Phi_uniform ? "OPTIM IMPROVES" : "NO IMPROVEMENT");
    std::printf("fluid fraction = %.4f  (target %.2f, gVol=%+.4f)\n", fluidFrac,
                vFrac, fluidFrac / vFrac - 1.0);
    std::printf("grey fraction (gamma in [0.1,0.9]) = %.3f  (low -> binary)\n", gray);
    std::printf("inlet column fluid frac = %.3f  outlet column fluid frac = %.3f  -> %s\n",
                wIn, wOut, (wIn > wOut + 0.05) ? "CONVERGENT (wide in -> narrow out)"
                                               : "not clearly convergent");
    std::printf("max |u| = %.4f\n", umax);
    std::printf("run time = %.1f s\n", secs);
    return 0;
}
