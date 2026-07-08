// Phase 5R -- Profiled-bore nozzle with a TRUE throat (convergent-divergent).
//
// Unlike the P4 `nozzle_axi` demo (constant bore r_in = a), the internal bore
// here follows r_in(z) = r_throat + K (z - H/2)^2 : a convergent-divergent
// contour with a real throat at mid-height. The structural wall lives in the
// body-fitted band r_in(z) <= r <= r_out(z) = r_in(z) + wall_max, meshed with
// the VALIDATED axisymmetric Q4 machinery fed with MAPPED node radii
// (Grid2DAxi::setNodeRadii): the isoparametric Jacobian already handles the
// distorted elements -- no new physics.
//
// TO: minimise wall mass subject to a von Mises p-norm stress constraint under
// an internal pressure on the profiled bore. Because the thick-wall hoop stress
// peaks where the radius is smallest, the optimiser reinforces the throat.
// Driver: MMA + density filter + Heaviside projection + the FD-validated
// AxiStressAdjoint sensitivity. CPU double precision (Eigen).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

#include "adjoint/AxiStressAdjoint.hpp"
#include "core/Grid2DAxi.hpp"
#include "core/Grid3D.hpp"
#include "io/PngWriter.hpp"
#include "io/STLExporter.hpp"
#include "topopt/MMAOptimizer.hpp"
#include "topopt/StressModelAxi.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;
using Mat = Eigen::MatrixXd;

namespace {

// --- Nozzle contour --------------------------------------------------------
struct Nozzle {
    double H = 4.0;
    double rThroat = 0.6;    // inner radius at the throat (mid-height)
    double K = 0.20;         // parabola curvature: r_in(0)=rThroat+K*(H/2)^2
    double wall = 0.70;      // radial thickness of the design band
    double rIn(double z) const {
        const double d = z - 0.5 * H;
        return rThroat + K * d * d;
    }
    double rOut(double z) const { return rIn(z) + wall; }
};

// Per-node radius map r(i,j) = r_in(z_j) + i * wall/nr, body-fitted to the bore.
std::vector<double> nozzleNodeRadii(const Grid2DAxi& g, const Nozzle& nz) {
    std::vector<double> rmap(static_cast<size_t>(g.nNodes()));
    for (int j = 0; j <= g.nz(); ++j) {
        const double z = g.z(j);
        const double ri = nz.rIn(z), ro = nz.rOut(z);
        for (int i = 0; i <= g.nr(); ++i)
            rmap[static_cast<size_t>(g.nodeId(i, j))] =
                ri + i * (ro - ri) / g.nr();
    }
    return rmap;
}

// 2D density filter (linear hat, radius rmin cells). Symmetric -> same operator
// maps sensitivities back (chain rule). Row-major elem (ei,ej). (from nozzle_axi)
struct DensityFilter {
    const Grid2DAxi& g;
    double rmin;
    std::vector<std::vector<std::pair<int, double>>> wts;

    DensityFilter(const Grid2DAxi& grid, double r) : g(grid), rmin(r) {
        const int nr = g.nr(), nz = g.nz();
        wts.resize(static_cast<size_t>(nr * nz));
        const int R = static_cast<int>(std::ceil(rmin));
        for (int ej = 0; ej < nz; ++ej)
            for (int ei = 0; ei < nr; ++ei) {
                const int e = g.elemId(ei, ej);
                double wsum = 0.0;
                for (int dj = -R; dj <= R; ++dj)
                    for (int di = -R; di <= R; ++di) {
                        const int ni = ei + di, nj = ej + dj;
                        if (ni < 0 || ni >= nr || nj < 0 || nj >= nz) continue;
                        const double dist = std::sqrt(double(di * di + dj * dj));
                        const double w = rmin - dist;
                        if (w <= 0.0) continue;
                        wts[static_cast<size_t>(e)].push_back({g.elemId(ni, nj), w});
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

// Heaviside projection (Wang-Lazarov-Sigmund), input clamped to [0,1] (LL-008).
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

// Revolved (Pappus) element volume weight: r_centroid * planar area (shoelace).
Vec elementVolumeWeights(const Grid2DAxi& g) {
    Vec w(g.nElems());
    for (int ej = 0; ej < g.nz(); ++ej)
        for (int ei = 0; ei < g.nr(); ++ei) {
            // CCW corner loop: (ei,ej)->(ei+1,ej)->(ei+1,ej+1)->(ei,ej+1).
            const double r0 = g.rNode(ei, ej), z0 = g.z(ej);
            const double r1 = g.rNode(ei + 1, ej), z1 = g.z(ej);
            const double r2 = g.rNode(ei + 1, ej + 1), z2 = g.z(ej + 1);
            const double r3 = g.rNode(ei, ej + 1), z3 = g.z(ej + 1);
            const double area =
                0.5 * std::fabs(r0 * z1 - r1 * z0 + r1 * z2 - r2 * z1 +
                                r2 * z3 - r3 * z2 + r3 * z0 - r0 * z3);
            const double rc = 0.25 * (r0 + r1 + r2 + r3);
            w(g.elemId(ei, ej)) = rc * area;
        }
    return w;
}

} // namespace

int main() {
    const Nozzle noz;
    const int nr = 24, nz = 80;

    // Body-fitted axisymmetric mesh: rectangular index space, mapped radii.
    Grid2DAxi grid(nr, nz, /*a=*/noz.rThroat, /*b=*/noz.rThroat + noz.wall,
                   noz.H);
    grid.setNodeRadii(nozzleNodeRadii(grid, noz));

    // Plane-strain slice: u_z = 0 on z=0 and z=H (hoop stiffness makes K SPD,
    // no radial pin needed -- same BC family as the validated Lame test).
    std::vector<int> fixed;
    for (int i = 0; i <= nr; ++i) {
        fixed.push_back(2 * grid.nodeId(i, 0) + 1);
        fixed.push_back(2 * grid.nodeId(i, nz) + 1);
    }

    // Internal pressure on the profiled bore, peaking at the throat: the
    // chamber pressure (and heat flux) are highest near the throat, so the
    // stress peaks there -- this is what drives throat wall reinforcement
    // (a constant pressure over this band would instead peak at the ends, where
    // the relative wall b/a is thinnest). Gaussian bump, as in P4 nozzle_axi.
    const double p0 = 1.0, bump = 3.0, sig = 0.18 * noz.H;
    std::vector<double> pRow(static_cast<size_t>(grid.nzn()));
    for (int j = 0; j <= nz; ++j) {
        const double t = (grid.z(j) - 0.5 * noz.H) / sig;
        pRow[static_cast<size_t>(j)] = p0 * (1.0 + bump * std::exp(-0.5 * t * t));
    }
    FEM2DAxi femLoad(grid, 0.3);
    const Vec F = femLoad.pressureLoadInnerProfiled(pRow);

    AxiStressAdjoint::Material mat;  // E0=1, Emin=1e-4, p=3, nu=0.3
    AxiStressAdjoint adj(grid, mat, fixed, F);
    StressModelAxi sm(mat.nu, /*qRelax=*/0.5, /*Pagg=*/8.0);
    DensityFilter filt(grid, /*rmin=*/2.0);

    const int ne = grid.nElems();
    const Vec w = elementVolumeWeights(grid);
    const double wtot = w.sum();

    // Stress limit relative to the solid design's aggregate (active constraint).
    const Vec rhoSolid = Vec::Ones(ne);
    const double sigmaSolid = adj.stressPNorm(filt.apply(rhoSolid), sm);
    const double sigmaLim = 1.6 * sigmaSolid;
    std::printf("nozzle: throat r_in=%.2f, ends r_in=%.2f, wall band=%.2f\n",
                noz.rIn(0.5 * noz.H), noz.rIn(0.0), noz.wall);
    std::printf("solid sigma_PN = %.4f  -> sigma_lim = %.4f\n",
                sigmaSolid, sigmaLim);

    // MMA: 1 design var per element, 1 stress constraint. Mass objective.
    MMAOptimizer::Params mp;
    mp.move = 0.12;  // conservative: stress-constrained TO is stiff at high beta
    MMAOptimizer mma(ne, 1, mp);
    const Vec xmin = Vec::Constant(ne, 1e-3), xmax = Vec::Ones(ne);
    Vec rho = Vec::Constant(ne, 0.6);
    const double eta = 0.5;

    // Gentle Heaviside continuation, capped at beta=4: a stress-constrained
    // design collapses if the projection sharpens too fast (the p-norm spikes
    // and MMA overshoots). beta<=4 gives a crisp-enough wall while staying safe.
    auto betaAt = [](int it) -> double {
        if (it <= 20) return 1.0;
        if (it <= 40) return 2.0;
        if (it <= 60) return 3.0;
        return 4.0;
    };

    const int nIter = 80;
    double mass = 0.0, gcon = 0.0, sigPN = 0.0;
    std::printf("%4s %8s %10s %6s %8s %6s\n", "it", "mass", "sigma_PN", "beta",
                "g", "gray");
    for (int it = 1; it <= nIter; ++it) {
        const double beta = betaAt(it);
        const Vec rhoTil = filt.apply(rho);
        Vec gamma(ne), dHdT(ne);
        for (int e = 0; e < ne; ++e) {
            gamma(e) = heaviside(rhoTil(e), beta, eta);
            dHdT(e) = dHeaviside(rhoTil(e), beta, eta);
        }

        // Objective: normalised mass + chain-ruled gradient.
        mass = w.dot(gamma) / wtot;
        Vec dMdT(ne);
        for (int e = 0; e < ne; ++e) dMdT(e) = (w(e) / wtot) * dHdT(e);
        const Vec df0 = filt.applyT(dMdT);

        // Constraint: g = sigma_PN(gamma)/sigma_lim - 1 <= 0 + chain-ruled grad.
        const auto ss = adj.stressPNormGrad(gamma, sm);
        sigPN = ss.J;
        gcon = sigPN / sigmaLim - 1.0;
        Vec dGdT(ne);
        for (int e = 0; e < ne; ++e) dGdT(e) = (ss.grad(e) / sigmaLim) * dHdT(e);
        const Vec dg1 = filt.applyT(dGdT);

        Vec fvals(1); fvals(0) = gcon;
        Mat dfdx(1, ne); dfdx.row(0) = dg1.transpose();

        rho = mma.step(rho, mass, df0, fvals, dfdx, xmin, xmax);

        if (it == 1 || it % 5 == 0 || it == nIter) {
            int ngi = 0;
            for (int e = 0; e < ne; ++e)
                if (gamma(e) > 0.1 && gamma(e) < 0.9) ++ngi;
            std::printf("%4d %8.4f %10.4f %6.0f %+8.4f %6.3f\n", it, mass, sigPN,
                        beta, gcon, double(ngi) / ne);
        }
    }

    // Final physical density at the last beta.
    const double betaF = betaAt(nIter);
    const Vec rhoTil = filt.apply(rho);
    Vec gamma(ne);
    for (int e = 0; e < ne; ++e) gamma(e) = heaviside(rhoTil(e), betaF, eta);

    // Grey measure (fraction of intermediate cells) -> quasi-binary check.
    int ng = 0;
    for (int e = 0; e < ne; ++e)
        if (gamma(e) > 0.1 && gamma(e) < 0.9) ++ng;
    const double gray = double(ng) / ne;

    // Wall-thickening diagnostic: mean filled radial fraction per z-band. A
    // higher value at the throat means the optimiser reinforced it.
    auto filledFrac = [&](int j0, int j1) {
        double s = 0.0;
        for (int ej = j0; ej < j1; ++ej)
            for (int ei = 0; ei < nr; ++ei) s += gamma(grid.elemId(ei, ej));
        return s / ((j1 - j0) * nr);
    };
    const double fThroat = filledFrac(nz / 2 - 6, nz / 2 + 6);
    const double fEnds = 0.5 * (filledFrac(0, 12) + filledFrac(nz - 12, nz));
    std::printf("done: mass=%.4f sigma_PN=%.4f g=%+.4f gray=%.3f\n",
                mass, sigPN, gcon, gray);
    std::printf("filled wall fraction: throat=%.3f  ends=%.3f  ratio=%.2f -> %s\n",
                fThroat, fEnds, fThroat / fEnds,
                (fThroat > fEnds * 1.05) ? "THICKER AT THROAT (expected)"
                                         : "no clear throat reinforcement");

    // --- Cross-section PNG/PGM (r,z) with the TRUE throat visible ------------
    // Full-diameter view x in [-Rmax, Rmax]: black bore (pinched at the throat),
    // white material walls following the contour, grey revolution axis line.
    {
        const double Rmax = noz.rOut(0.0);              // widest outer radius
        const int Wpix = 240, sc = 3, Hpix = nz * sc;   // upscale z for aspect
        const double dx = 2.0 * Rmax / Wpix;
        std::vector<unsigned char> img(static_cast<size_t>(Wpix) * Hpix, 0);
        for (int row = 0; row < Hpix; ++row) {
            const int ej = nz - 1 - row / sc;           // top of image = z=H
            const double zc = grid.z(ej) + 0.5 * grid.hz();
            const double ri = noz.rIn(zc), ro = noz.rOut(zc);
            for (int col = 0; col < Wpix; ++col) {
                const double x = -Rmax + (col + 0.5) * dx;
                const double rr = std::fabs(x);
                unsigned char px = 0;                   // void/outside -> black
                if (std::fabs(x) < 0.6 * dx) {
                    px = 110;                            // revolution axis (r=0)
                } else if (rr >= ri && rr < ro) {
                    int i = static_cast<int>((rr - ri) / (ro - ri) * nr);
                    i = std::clamp(i, 0, nr - 1);
                    const double v = std::clamp(gamma(grid.elemId(i, ej)), 0.0, 1.0);
                    px = static_cast<unsigned char>(std::lround(255.0 * v));
                }
                img[static_cast<size_t>(row) * Wpix + col] = px;
            }
        }
        PngWriter::writeGray("output/nozzle_profiled.png", Wpix, Hpix, img);
        std::printf("wrote output/nozzle_profiled.png (%dx%d; black=void/bore,"
                    " white=material, grey line=axis r=0)\n", Wpix, Hpix);
    }

    // --- Revolved 3D STL of the profiled nozzle (voxel surface) --------------
    {
        const double Rmax = noz.rOut(0.0);
        const int nxy = 72, nz3 = nz;
        Grid3D g3(nxy, nxy, nz3);
        Vec rho3(g3.nElems());
        const double hxy = 2.0 * Rmax / nxy;
        for (int kz = 0; kz < nz3; ++kz) {
            const double zc = grid.z(kz) + 0.5 * grid.hz();
            const double ri = noz.rIn(zc), ro = noz.rOut(zc);
            for (int jy = 0; jy < nxy; ++jy)
                for (int ix = 0; ix < nxy; ++ix) {
                    const double x = -Rmax + (ix + 0.5) * hxy;
                    const double y = -Rmax + (jy + 0.5) * hxy;
                    const double rr = std::sqrt(x * x + y * y);
                    double v = 0.0;
                    if (rr >= ri && rr < ro) {
                        int i = std::clamp(int((rr - ri) / (ro - ri) * nr), 0,
                                           nr - 1);
                        v = gamma(grid.elemId(i, kz));
                    }
                    rho3(g3.elemId(ix, jy, kz)) = v;
                }
        }
        STLExporter::writeVoxelSurface("output/nozzle_profiled.stl", rho3, g3,
                                       0.5);
        std::printf("wrote output/nozzle_profiled.stl (revolved wall, %d^2x%d "
                    "voxels)\n", nxy, nz3);
    }
    return 0;
}
