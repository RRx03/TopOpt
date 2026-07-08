// PHASE 5 demonstrator: end-to-end multiphysics topology optimisation.
//
// Minimise J = Fmech^T U (compliance through the fluid->thermal->elastic
// cascade) subject to a fluid-volume-fraction constraint, driven by MMA with
// the FD-validated TripleAdjoint gradient (gate DF 2.1e-7). Density field is
// regularised by a 3D linear-hat filter and pushed towards black/white by a
// Heaviside projection with beta continuation.
//
// Physical picture (a schematic cooling jacket): a coolant is driven along z by
// a body force; a heat source Q is peaked at mid-height (the "throat"); a
// mechanical cantilever load makes J = Fmech^T U sensitive. gamma = 1 is fluid
// (low Brinkman drag, carries heat away), gamma = 0 is solid (stiff, blocks
// flow). The optimiser trades mechanical stiffness against convective cooling
// of the throat under a limited fluid budget.
//
// Pipeline per MMA iteration:
//   rho --filter W--> rho_tilde --Heaviside(beta)--> rho_bar = gamma
//   gamma --TripleAdjoint::solve--> J, dJ/dgamma
//   chain: dJ/drho = W^T ( dHeaviside/drho_tilde .* dJ/dgamma )
//
// CPU double precision (Eigen direct solves inside TripleAdjoint). No Metal.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "adjoint/TripleAdjoint.hpp"
#include "core/Grid3D.hpp"
#include "io/VTKExporter.hpp"
#include "topopt/MMAOptimizer.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;
using Mat = Eigen::MatrixXd;

namespace {

// 3D density filter (linear hat, radius rmin cells). Rows are normalised to sum
// to 1 (weighted average), so applyT is the exact transpose used to chain the
// sensitivities. Neighbour lists cached once.
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

// Heaviside projection (Wang-Lazarov-Sigmund), eta = 0.5, input clamped to
// [0,1] first (LL-008: never feed a tanh/pow a value outside the design box).
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

// Average an nNodes nodal scalar over an element's 8 corners -> cell value.
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

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // stream progress when piped
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();

    // ---- geometry: modest box, coolant flows along +z, throat at mid-z. ----
    const int nx = 12, ny = 12, nz = 20;
    Grid3D g(nx, ny, nz);
    const int nN = g.nNodes();
    const int ne = g.nElems();
    std::printf("cooling_jacket: grid %dx%dx%d  (%d elems, Stokes DOF=%d)\n", nx,
                ny, nz, ne, 4 * nN);

    // ---- physics parameters (documented). ----
    TripleAdjoint::Params prm;
    prm.mu = 1.0;
    prm.alphaStab = 1.0 / 12.0;
    prm.alphaMax = 5.0e2;   // Brinkman drag on solid cells (well-conditioned).
    prm.alphaMin = 0.0;
    prm.qBrink = 0.1;
    prm.ks = 1.0;           // solid conductivity (gamma = 0)
    prm.kf = 0.3;           // fluid conductivity (gamma = 1); advection carries heat
    prm.E0 = 1.0;
    prm.Emin = 1e-4;
    prm.p = 3.0;
    prm.alphaTh = 1.5e-2;   // thermo-elastic coupling: throat heat -> displacement
    prm.Tref = 0.0;
    prm.nu = 0.3;

    const std::array<double, 3> bodyForce = {0.0, 0.0, 30.0};  // drive coolant +z

    // Stokes BCs: no-slip x-walls, slip (u_y=0) on y-faces, one pressure datum.
    std::vector<int> stokesFixed;
    for (int k = 0; k <= nz; ++k)
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i <= nx; ++i) {
                const int n = g.nodeId(i, j, k);
                if (i == 0 || i == nx) {
                    stokesFixed.push_back(TripleAdjoint::sdof(n, 0));
                    stokesFixed.push_back(TripleAdjoint::sdof(n, 1));
                    stokesFixed.push_back(TripleAdjoint::sdof(n, 2));
                } else if (j == 0 || j == ny) {
                    stokesFixed.push_back(TripleAdjoint::sdof(n, 1));
                }
            }
    stokesFixed.push_back(TripleAdjoint::sdof(g.nodeId(0, 0, 0), 3));

    // Thermal: cold coolant reservoirs at z=0 and z=nz (T=0), plus a volumetric
    // heat source Q peaked at mid-z (the throat). The interior heats up unless
    // fluid convects the heat toward the cold ends.
    std::vector<std::uint8_t> dirMask(static_cast<size_t>(nN), 0);
    Vec dirVal = Vec::Zero(nN);
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i) {
            const int n0 = g.nodeId(i, j, 0);
            const int n1 = g.nodeId(i, j, nz);
            dirMask[static_cast<size_t>(n0)] = 1; dirVal(n0) = 0.0;
            dirMask[static_cast<size_t>(n1)] = 1; dirVal(n1) = 0.0;
        }
    Vec Q = Vec::Zero(nN);
    {
        const double zc = 0.5 * nz, sig = 0.12 * nz, Q0 = 1.5;
        for (int k = 0; k <= nz; ++k) {
            const double t = (double(k) - zc) / sig;
            const double qz = Q0 * std::exp(-0.5 * t * t);
            for (int j = 0; j <= ny; ++j)
                for (int i = 0; i <= nx; ++i) Q(g.nodeId(i, j, k)) = qz;
        }
    }

    // Elastic: clamp the z=0 base, apply a +z tension load on the z=nz top face.
    // The hot throat swells and pushes the top face in +z, i.e. ALONG the load,
    // so heating the throat does positive work and raises J = Fmech^T U (the
    // coupling is symmetric, hence robust). Minimising J therefore rewards
    // cooling the throat: fluid becomes beneficial and the fluid-budget
    // constraint turns active, concentrating coolant channels at the throat.
    std::vector<int> elasticFixed;
    Vec Fmech = Vec::Zero(g.nDof());
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i) {
            const int nb = g.nodeId(i, j, 0);
            elasticFixed.push_back(3 * nb + 0);
            elasticFixed.push_back(3 * nb + 1);
            elasticFixed.push_back(3 * nb + 2);
            Fmech(3 * g.nodeId(i, j, nz) + 2) = 1.0e-2;  // +z tension load
        }

    TripleAdjoint adj(g, prm, stokesFixed, bodyForce, dirMask, dirVal, Q,
                      elasticFixed, Fmech);

    // ---- density filter + Heaviside + MMA setup. ----
    DensityFilter3D filt(g, /*rmin=*/1.5);
    const double eta = 0.5, vFrac = 0.40;

    MMAOptimizer::Params mp;
    mp.move = 0.2;  // conservative move limit for a stiff multiphysics objective
    MMAOptimizer mma(ne, 1, mp);
    const Vec xmin = Vec::Constant(ne, 0.0), xmax = Vec::Ones(ne);
    Vec rho = Vec::Constant(ne, vFrac);  // start at the fluid budget

    const int nIter = 60;
    double Jref = 0.0, J = 0.0, fluidFrac = 0.0, gVol = 0.0, gray = 0.0;
    double J_first = 0.0;

    auto betaAt = [](int it) -> double {
        if (it <= 12) return 1.0;
        if (it <= 24) return 2.0;
        if (it <= 36) return 4.0;
        if (it <= 48) return 8.0;
        return 16.0;
    };

    std::printf("%4s %8s %10s %6s %8s %7s %9s\n", "it", "J", "J/Jref", "beta",
                "fluid", "gVol", "gray");
    for (int it = 1; it <= nIter; ++it) {
        const double beta = betaAt(it);

        const Vec rhoTil = filt.apply(rho);
        Vec gamma(ne), dHdT(ne);
        for (int e = 0; e < ne; ++e) {
            gamma(e) = heaviside(rhoTil(e), beta, eta);
            dHdT(e) = dHeaviside(rhoTil(e), beta, eta);
        }

        const auto sol = adj.solve(gamma);
        J = sol.J;
        if (it == 1) { Jref = std::fabs(J) > 1e-30 ? std::fabs(J) : 1.0; J_first = J; }

        // Objective chain: dJ/drho = W^T ( dH/drho_tilde .* dJ/dgamma ).
        Vec dJdT(ne);
        for (int e = 0; e < ne; ++e) dJdT(e) = sol.grad(e) * dHdT(e);
        const Vec df0 = filt.applyT(dJdT) / Jref;

        // Volume constraint: mean(gamma) / vFrac - 1 <= 0.
        fluidFrac = gamma.mean();
        gVol = fluidFrac / vFrac - 1.0;
        Vec dVdT(ne);
        for (int e = 0; e < ne; ++e) dVdT(e) = (1.0 / ne) * dHdT(e) / vFrac;
        const Vec dgVol = filt.applyT(dVdT);

        Vec fvals(1); fvals(0) = gVol;
        Mat dfdx(1, ne); dfdx.row(0) = dgVol.transpose();

        rho = mma.step(rho, J / Jref, df0, fvals, dfdx, xmin, xmax);

        // Grey measure: fraction of cells with gamma in [0.1, 0.9].
        int ng = 0;
        for (int e = 0; e < ne; ++e)
            if (gamma(e) > 0.1 && gamma(e) < 0.9) ++ng;
        gray = double(ng) / ne;

        if (it == 1 || it % 5 == 0 || it == nIter)
            std::printf("%4d %8.3e %10.4f %6.0f %8.4f %+7.4f %8.3f\n", it, J,
                        J / Jref, beta, fluidFrac, gVol, gray);
    }

    // ---- final fields (recompute at final beta for reporting/export). ----
    const double betaF = betaAt(nIter);
    const Vec rhoTil = filt.apply(rho);
    Vec gamma(ne);
    for (int e = 0; e < ne; ++e) gamma(e) = heaviside(rhoTil(e), betaF, eta);
    const auto sol = adj.solve(gamma);

    // Cell fields for ParaView: density, coolant speed, temperature, |U|.
    Vec speed = Vec::Zero(nN), umag = Vec::Zero(nN);
    for (int n = 0; n < nN; ++n) {
        const double ux = sol.w(4 * n + 0), uy = sol.w(4 * n + 1),
                     uz = sol.w(4 * n + 2);
        speed(n) = std::sqrt(ux * ux + uy * uy + uz * uz);
        const double dx = sol.U(3 * n + 0), dy = sol.U(3 * n + 1),
                     dz = sol.U(3 * n + 2);
        umag(n) = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    const Vec speedC = nodalScalarToCell(g, speed);
    const Vec tempC = nodalScalarToCell(g, sol.T);
    const Vec umagC = nodalScalarToCell(g, umag);

    VTKExporter::writeImageData("output/cooling_jacket.vti", g,
                                {{"density", &gamma},
                                 {"speed", &speedC},
                                 {"temperature", &tempC},
                                 {"displacement", &umagC}});

    // ---- sanity metrics. ----
    fluidFrac = gamma.mean();
    int ng = 0;
    for (int e = 0; e < ne; ++e)
        if (gamma(e) > 0.1 && gamma(e) < 0.9) ++ng;
    gray = double(ng) / ne;

    auto bandFluid = [&](int k0, int k1) {
        double s = 0.0; int c = 0;
        for (int ez = k0; ez < k1; ++ez)
            for (int ey = 0; ey < ny; ++ey)
                for (int ex = 0; ex < nx; ++ex) { s += gamma(g.elemId(ex, ey, ez)); ++c; }
        return s / c;
    };
    const int band = 3;
    const double fThroat = bandFluid(nz / 2 - band, nz / 2 + band);
    const double fEnds = 0.5 * (bandFluid(0, band) + bandFluid(nz - band, nz));

    const double secs =
        std::chrono::duration<double>(clk::now() - t0).count();

    std::printf("\n==== cooling_jacket summary ====\n");
    std::printf("J: first=%.4e  final=%.4e  (ratio %.3f)\n", J_first, sol.J,
                sol.J / (std::fabs(J_first) > 1e-30 ? J_first : 1.0));
    std::printf("fluid fraction = %.4f  (target %.2f, gVol=%+.4f)\n", fluidFrac,
                vFrac, fluidFrac / vFrac - 1.0);
    std::printf("grey fraction (gamma in [0.1,0.9]) = %.3f\n", gray);
    std::printf("fluid density: throat=%.3f  ends=%.3f  ratio=%.2f -> %s\n",
                fThroat, fEnds, fThroat / std::max(fEnds, 1e-9),
                (fThroat > 1.1 * fEnds) ? "MORE FLUID AT THROAT"
                                        : "no clear throat concentration");
    std::printf("max coolant speed = %.4f   T range [%.4f, %.4f]\n",
                adj.maxSpeed(sol.w), sol.T.minCoeff(), sol.T.maxCoeff());
    std::printf("run time = %.1f s   wrote output/cooling_jacket.vti\n", secs);
    return 0;
}
