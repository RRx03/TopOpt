// Axisymmetric structural nozzle-wall demo (Phase 4 step 7b).
// Minimise mass of an annular wall subject to a von Mises (p-norm) stress
// constraint, under an internal pressure that peaks at the throat (mid-height).
// Driven by MMA, stress sensitivity from the FD-validated AxiStressAdjoint.
// Expected outcome: material concentrates near the throat and the inner radius
// (where Lamé hoop stress + the pressure bump make the stress peak).
//
// CPU double precision (Eigen). Structural only — thermal coupling in
// axisymmetric is deferred (see PHASE_4_REPORT / handoff).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

#include "adjoint/AxiStressAdjoint.hpp"
#include "core/Grid2DAxi.hpp"
#include "core/Grid3D.hpp"
#include "io/STLExporter.hpp"
#include "io/VTKExporter.hpp"
#include "topopt/MMAOptimizer.hpp"
#include "topopt/StressModelAxi.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;
using Mat = Eigen::MatrixXd;

namespace {

// Simple 2D density filter (linear hat, radius rmin cells). Symmetric, so the
// same operator maps sensitivities back (chain rule). Row-major elem (ei,ej).
struct DensityFilter {
    const Grid2DAxi& g;
    double rmin;
    std::vector<std::vector<std::pair<int, double>>> wts;  // per-element neighbours

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
    // Transpose (chain rule for sensitivities). Filter is a weighted average;
    // its transpose scatters with the same weights.
    Vec applyT(const Vec& g_in) const {
        Vec out = Vec::Zero(g_in.size());
        for (size_t e = 0; e < wts.size(); ++e)
            for (const auto& pr : wts[e])
                out(pr.first) += pr.second * g_in(static_cast<Eigen::Index>(e));
        return out;
    }
};

// Internal pressure profile p(z), a Gaussian bump at the throat (mid-height).
double pressureAt(double z, double H, double p0, double bump) {
    const double zc = 0.5 * H, sigma = 0.18 * H;
    const double t = (z - zc) / sigma;
    return p0 * (1.0 + bump * std::exp(-0.5 * t * t));
}

// Build the nodal force vector for a z-varying internal pressure on r=a.
Vec buildPressureLoad(const Grid2DAxi& g, double p0, double bump) {
    Vec F = Vec::Zero(g.nDof());
    for (int j = 0; j <= g.nz(); ++j) {
        const double w = (j == 0 || j == g.nz()) ? 0.5 : 1.0;
        const double p = pressureAt(g.z(j), g.height(), p0, bump);
        F(2 * g.nodeId(0, j) + 0) += p * g.a() * g.hz() * w;  // radial, 2*pi omitted
    }
    return F;
}

} // namespace

int main() {
    // Geometry: annular wall, inner radius a, outer b, height H.
    const int nr = 24, nz = 60;
    const double a = 1.0, b = 1.6, H = 4.0;
    Grid2DAxi grid(nr, nz, a, b, H);

    // Plane-strain slice: u_z = 0 on z=0 and z=H faces.
    std::vector<int> fixed;
    for (int i = 0; i <= nr; ++i) {
        fixed.push_back(2 * grid.nodeId(i, 0) + 1);
        fixed.push_back(2 * grid.nodeId(i, nz) + 1);
    }
    // Pin one node radially to remove the rigid axial/rotational nullspace cleanly.
    fixed.push_back(2 * grid.nodeId(nr, 0) + 0);

    const Vec F = buildPressureLoad(grid, /*p0=*/1.0, /*bump=*/3.0);

    AxiStressAdjoint::Material mat;  // E0=1, Emin=1e-4, p=3, nu=0.3
    AxiStressAdjoint adj(grid, mat, fixed, F);
    StressModelAxi sm(mat.nu, /*qRelax=*/0.5, /*Pagg=*/8.0);
    DensityFilter filt(grid, /*rmin=*/1.6);

    const int ne = grid.nElems();
    // Element volume weights (2*pi r dr dz, 2*pi omitted): mass = sum w_e rhoPhys_e.
    Vec w(ne);
    for (int ej = 0; ej < nz; ++ej)
        for (int ei = 0; ei < nr; ++ei)
            w(grid.elemId(ei, ej)) = grid.r(ei) * grid.hr() * grid.hz();
    const double wtot = w.sum();

    // Stress limit: relative to the solid design's aggregated stress, so the
    // constraint is active and the optimiser can shed material.
    Vec rhoSolid = Vec::Ones(ne);
    const double sigmaSolid = adj.stressPNorm(filt.apply(rhoSolid), sm);
    const double sigmaLim = 2.0 * sigmaSolid;
    std::printf("solid sigma_PN = %.4f  -> sigma_lim = %.4f\n", sigmaSolid, sigmaLim);

    // MMA setup: 1 design var per element, 1 stress constraint.
    MMAOptimizer::Params mp;
    MMAOptimizer mma(ne, 1, mp);
    const Vec xmin = Vec::Constant(ne, 1e-3), xmax = Vec::Ones(ne);
    Vec rho = Vec::Constant(ne, 0.5);

    int it = 0;
    double mass = 0.0, gcon = 0.0;
    for (it = 1; it <= 80; ++it) {
        const Vec rhoPhys = filt.apply(rho);

        // Objective: mass (normalised by solid mass) + gradient.
        mass = w.dot(rhoPhys) / wtot;
        const Vec dmass_drhoPhys = w / wtot;
        const Vec df0 = filt.applyT(dmass_drhoPhys);

        // Constraint: g1 = sigma_PN/sigma_lim - 1 <= 0 + gradient.
        const auto ss = adj.stressPNormGrad(rhoPhys, sm);
        gcon = ss.J / sigmaLim - 1.0;
        const Vec dg1 = filt.applyT(ss.grad) / sigmaLim;

        Vec fvals(1); fvals(0) = gcon;
        Mat dfdx(1, ne); dfdx.row(0) = dg1.transpose();

        rho = mma.step(rho, mass, df0, fvals, dfdx, xmin, xmax);

        if (it % 5 == 0 || it == 1)
            std::printf("it %3d | mass=%.4f | sigma_PN=%.4f | g=%+.4f\n",
                        it, mass, ss.J, gcon);
    }

    // Output the final filtered density field as a PGM image (rows=z, cols=r).
    const Vec rhoPhys = filt.apply(rho);
    std::ofstream pgm("output/nozzle_axi.pgm", std::ios::binary);
    pgm << "P5\n" << nr << " " << nz << "\n255\n";
    for (int ej = nz - 1; ej >= 0; --ej)
        for (int ei = 0; ei < nr; ++ei) {
            const double v = rhoPhys(grid.elemId(ei, ej));
            pgm.put(static_cast<char>(std::lround(255.0 * std::clamp(v, 0.0, 1.0))));
        }
    pgm.close();

    // Quick quantitative summary of where the material sits (throat vs ends).
    auto bandMass = [&](int j0, int j1) {
        double m = 0.0;
        for (int ej = j0; ej < j1; ++ej)
            for (int ei = 0; ei < nr; ++ei)
                m += rhoPhys(grid.elemId(ei, ej));
        return m / ((j1 - j0) * nr);
    };
    const double mThroat = bandMass(nz / 2 - 5, nz / 2 + 5);
    const double mEnd = 0.5 * (bandMass(0, 10) + bandMass(nz - 10, nz));
    std::printf("done: mass=%.4f sigma_PN=%.4f g=%+.4f\n", mass,
                adj.stressPNorm(rhoPhys, sm), gcon);
    std::printf("mean density: throat=%.3f  ends=%.3f  ratio=%.2f -> %s\n",
                mThroat, mEnd, mThroat / mEnd,
                (mThroat > mEnd * 1.1) ? "THICKER AT THROAT (expected)"
                                       : "no clear throat reinforcement");
    std::printf("wrote output/nozzle_axi.pgm (%dx%d, rows=z cols=r; WHITE=material)\n",
                nr, nz);

    // Unambiguous full-diameter cross-section: x in [-b,b]. Hollow bore (r<a) in
    // black, the two walls (a<=|x|<=b) in their density. Reads as a tube section:
    // black bore in the middle, white walls on both sides, whiter at the throat.
    {
        const int boreHalf = static_cast<int>(std::lround(a / grid.hr()));
        const int Wf = nr + 2 * boreHalf + nr;
        std::ofstream cs("output/nozzle_crosssection.pgm", std::ios::binary);
        cs << "P5\n" << Wf << " " << nz << "\n255\n";
        for (int ej = nz - 1; ej >= 0; --ej) {
            auto px = [&](int ei) {
                return static_cast<char>(std::lround(
                    255.0 * std::clamp(rhoPhys(grid.elemId(ei, ej)), 0.0, 1.0)));
            };
            for (int ei = nr - 1; ei >= 0; --ei) cs.put(px(ei));  // left wall (b->a)
            // Bore (void). Mark the revolution axis (r=0) at the exact centre with
            // a thin grey line so the image is unambiguous: walls hug this axis-bore.
            for (int k = 0; k < 2 * boreHalf; ++k)
                cs.put((k == boreHalf || k == boreHalf - 1) ? char(110) : char(0));
            for (int ei = 0; ei < nr; ++ei) cs.put(px(ei));        // right wall (a->b)
        }
        std::printf("wrote output/nozzle_crosssection.pgm (%dx%d; black=void,"
                    " white=material, grey centre line=revolution axis r=0)\n",
                    Wf, nz);
    }

    // --- Revolve the axisymmetric design into a 3D STL (watertight voxels) ---
    // Sample the (r,z) density on a Cartesian voxel grid via r = sqrt(x^2+y^2),
    // then reuse the validated voxel-surface STL exporter (Phase 2).
    {
        const int nxy = 64;                 // voxels across the diameter
        const int nz3 = nz;                 // keep axial resolution
        Grid3D g3(nxy, nxy, nz3);
        Vec rho3(g3.nElems());
        const double hxy = 2.0 * b / nxy;   // domain [-b,b] in x,y
        auto axiCell = [&](double rr, int kz) -> double {
            if (rr < a || rr >= b) return 0.0;
            int ir = static_cast<int>((rr - a) / grid.hr());
            ir = std::clamp(ir, 0, nr - 1);
            int jz = std::clamp(kz, 0, nz - 1);
            return rhoPhys(grid.elemId(ir, jz));
        };
        for (int kz = 0; kz < nz3; ++kz)
            for (int jy = 0; jy < nxy; ++jy)
                for (int ix = 0; ix < nxy; ++ix) {
                    const double x = -b + (ix + 0.5) * hxy;
                    const double y = -b + (jy + 0.5) * hxy;
                    const double rr = std::sqrt(x * x + y * y);
                    rho3(g3.elemId(ix, jy, kz)) = axiCell(rr, kz);
                }
        // Threshold below the throat mean so the reinforced region renders as a
        // connected solid (the field peaks ~0.45; 0.5 would drop most of it).
        STLExporter::writeVoxelSurface("output/nozzle3d.stl", rho3, g3, 0.3);

        // VTK field export (density + von Mises) revolved to 3D -> ParaView.
        const auto ssFinal = adj.stressPNormGrad(rhoPhys, sm);
        const Vec vm = sm.vonMisesSolid(grid, ssFinal.U);  // per (r,z) element
        Vec vm3(g3.nElems());
        for (int kz = 0; kz < nz3; ++kz)
            for (int jy = 0; jy < nxy; ++jy)
                for (int ix = 0; ix < nxy; ++ix) {
                    const double x = -b + (ix + 0.5) * hxy;
                    const double y = -b + (jy + 0.5) * hxy;
                    const double rr = std::sqrt(x * x + y * y);
                    double v = 0.0;
                    if (rr >= a && rr < b) {
                        int ir = std::clamp(int((rr - a) / grid.hr()), 0, nr - 1);
                        v = vm(grid.elemId(ir, std::clamp(kz, 0, nz - 1)));
                    }
                    vm3(g3.elemId(ix, jy, kz)) = v;
                }
        VTKExporter::writeImageData(
            "output/nozzle3d.vti", g3,
            {{"density", &rho3}, {"vonMises", &vm3}});
    }
    return 0;
}
