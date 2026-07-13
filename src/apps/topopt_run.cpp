// topopt_run — the input-language driver. Reads a .topopt.json problem spec,
// builds the problem (grid + boundary conditions via BCResolver), runs the
// optimization on the validated solver, and writes results (VTK for ParaView,
// STL via marching cubes). Closes the loop: author JSON -> run -> visualize.
//
// v1: structural (compliance/mass minimization under a volume constraint) on the
// GPU matrix-free path. v2/v3 (thermo, fluid) extend the physics dispatch.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>

#include "adjoint/AxiStressAdjoint.hpp"
#include "adjoint/DissipationAdjoint.hpp"
#include "adjoint/ThermalObjectiveAdjoint.hpp"
#include "adjoint/ThermoElasticAdjoint.hpp"
#include "adjoint/TripleAdjoint.hpp"
#include "core/Grid2DAxi.hpp"
#include "core/Grid3D.hpp"
#include "fem/H8Element.hpp"
#include "filter/Helmholtz3D.hpp"
#include "gpu/CGSolver3D.hpp"
#include "io/BCResolver.hpp"
#include "io/MarchingCubes.hpp"
#include "io/PngWriter.hpp"
#include "io/ProblemSpec.hpp"
#include "io/STLExporter.hpp"
#include "io/VTKExporter.hpp"
#include "topopt/MMAOptimizer.hpp"
#include "topopt/SIMP3D.hpp"
#include "topopt/StressModel.hpp"
#include "topopt/StressModelAxi.hpp"

using namespace topopt;
using namespace topopt::gpu;
using Vec = Eigen::VectorXd;

namespace {

double volumeConstraint(const ProblemSpec& s) {
    for (const auto& c : s.constraints)
        if (c.type == "volume") return c.max;
    return 0.5;
}

// ===========================================================================
// Thermo-elastic branch (CPU double, ThermoElasticAdjoint). Mass minimisation
// under a von Mises p-norm stress constraint. Reuses the exact filter/Heaviside
// machinery of cooling_jacket.cpp; density convention rho=1 material, rho=0 void
// (matches ThermoElasticAdjoint: E_e = Emin + rho^p (E0-Emin)).
// ===========================================================================

// 3D density filter (linear hat, radius rmin cells). Rows normalised to sum 1,
// so applyT is the exact transpose used to chain sensitivities. (cf.
// cooling_jacket.cpp — reused verbatim.)
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

// Average an nNodes nodal scalar over each element's 8 corners -> cell value.
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

double vonMisesLimit(const ProblemSpec& s) {
    for (const auto& c : s.constraints)
        if (c.type == "vonmises") return c.max;
    return 0.0;
}

// Beta continuation schedule: spread spec.filter.heaviside.beta over max_iter.
double betaAt(const ProblemSpec& s, int it) {
    if (s.heaviside_beta.empty()) return 1.0;
    const int stages = static_cast<int>(s.heaviside_beta.size());
    const int per = std::max(1, s.max_iter / stages);
    int idx = (it - 1) / per;
    if (idx >= stages) idx = stages - 1;
    return s.heaviside_beta[static_cast<size_t>(idx)];
}

int runThermoElastic(const ProblemSpec& s) {
    Grid3D grid(s.grid[0], s.grid[1], s.grid[2]);
    const int ne = grid.nElems();
    const int nN = grid.nNodes();

    // --- boundary conditions from the input language. ---
    // Elastic: fixed-DOF mask -> list of fixed DOF indices; mechanical loads.
    const auto fixedMask = BCResolver::fixedMask(grid, s);
    std::vector<int> elasticFixedDofs;
    for (int d = 0; d < grid.nDof(); ++d)
        if (fixedMask[static_cast<size_t>(d)]) elasticFixedDofs.push_back(d);
    const Vec Fmech = BCResolver::loadVector(grid, s);
    // Thermal: Dirichlet nodes (T=0, homogeneous) + nodal heat source Q.
    const std::vector<int> thermalFixedNodes = BCResolver::thermalFixedNodes(grid, s);
    const Vec Q = BCResolver::thermalSource(grid, s);

    // --- material from the spec. ---
    ThermoElasticAdjoint::Material mat;
    mat.E0 = s.E0; mat.Emin = s.Emin; mat.p = s.penal;
    mat.k0 = s.k_solid; mat.kmin = 1e-4; mat.q = s.penal;
    mat.alpha = s.alpha_th; mat.Tref = s.Tref; mat.nu = s.nu;

    ThermoElasticAdjoint adj(grid, mat, elasticFixedDofs, thermalFixedNodes,
                             Fmech, Q);
    StressModel sm(mat.nu, /*qRelax=*/0.5, /*Pagg=*/8.0);

    const double sigLim = vonMisesLimit(s);
    if (sigLim <= 0.0) {
        std::fprintf(stderr,
                     "thermo-elastic: missing constraints[type=vonmises].max\n");
        return 1;
    }

    // --- filter + MMA setup. ---
    const double cell_mm = s.size_mm[0] / std::max(1, s.grid[0]);
    DensityFilter3D filt(grid, s.filter_radius_mm / cell_mm);
    const double eta = s.heaviside_eta;

    MMAOptimizer::Params mp;
    mp.move = 0.05;  // small move limit: the p-norm stress constraint is stiff and
                     // strongly non-convex; large steps overshoot into the cliff.
    mp.asyinit = 0.2;
    MMAOptimizer mma(ne, 1, mp);
    const Vec xmin = Vec::Constant(ne, 0.0), xmax = Vec::Ones(ne);
    Vec rho = Vec::Constant(ne, 0.5);  // start mid: near the stress-active mass

    std::printf(
        "topopt_run '%s' [thermo-elastic]: %dx%dx%d (%d elems), obj=mass, "
        "vonMises<=%.4g, %d iter\n",
        s.name.c_str(), s.grid[0], s.grid[1], s.grid[2], ne, sigLim, s.max_iter);
    std::printf("  BCs: %zu fixed DOF, %zu thermal Dirichlet nodes, "
                "|Fmech|=%.3e, |Q|=%.3e\n",
                elasticFixedDofs.size(), thermalFixedNodes.size(), Fmech.norm(),
                Q.norm());
    std::printf("%4s %8s %10s %6s %8s %7s\n", "it", "mass", "sigmaPN", "beta",
                "g1", "gray");

    // MMA has no late damping, so the stiff stress constraint makes the iterate
    // oscillate around the active boundary. Track the best design DESIGN (the
    // lowest-mass iterate that is feasible within tol) and report/export it —
    // standard practice for stress-constrained TO.
    const double feasTol = 0.02;  // accept up to 2% over the stress limit
    double mass = 1.0, sigPN = 0.0, g1 = 0.0;
    Vec bestRho = rho;
    double bestBeta = betaAt(s, 1), bestMass = 1e30;
    bool haveBest = false;
    for (int it = 1; it <= s.max_iter; ++it) {
        const double beta = betaAt(s, it);
        const Vec rhoTil = filt.apply(rho);
        Vec rhoBar(ne), dHdT(ne);
        for (int e = 0; e < ne; ++e) {
            rhoBar(e) = heaviside(rhoTil(e), beta, eta);
            dHdT(e) = dHeaviside(rhoTil(e), beta, eta);
        }

        // Objective: mass = mean(rhoBar). d mass/drho = W^T( dH .* 1/n ).
        mass = rhoBar.mean();
        Vec dMass(ne);
        for (int e = 0; e < ne; ++e) dMass(e) = (1.0 / ne) * dHdT(e);
        const Vec df0 = filt.applyT(dMass);

        // Constraint g1 = sigma_PN(rhoBar)/sigLim - 1 <= 0.
        const auto ss = adj.stressPNormGrad(rhoBar, sm);
        sigPN = ss.J;
        g1 = sigPN / sigLim - 1.0;
        Vec dG(ne);
        for (int e = 0; e < ne; ++e) dG(e) = ss.grad(e) * dHdT(e) / sigLim;
        const Vec dg1 = filt.applyT(dG);

        // Snapshot the current (feasible-enough, lowest-mass) design.
        if (g1 <= feasTol && mass < bestMass) {
            bestMass = mass; bestRho = rho; bestBeta = beta; haveBest = true;
        }

        Vec fvals(1); fvals(0) = g1;
        Eigen::MatrixXd dfdx(1, ne); dfdx.row(0) = dg1.transpose();

        rho = mma.step(rho, mass, df0, fvals, dfdx, xmin, xmax);

        int ng = 0;
        for (int e = 0; e < ne; ++e)
            if (rhoBar(e) > 0.1 && rhoBar(e) < 0.9) ++ng;
        const double gray = double(ng) / ne;

        if (it == 1 || it % 5 == 0 || it == s.max_iter)
            std::printf("%4d %8.4f %10.4e %6.0f %+7.4f %7.3f\n", it, mass, sigPN,
                        beta, g1, gray);
    }
    if (!haveBest) { bestRho = rho; bestBeta = betaAt(s, s.max_iter); }
    std::printf("best feasible design: mass=%.4f (beta=%.0f)\n", bestMass, bestBeta);

    // --- final fields on the best design (report + export). ---
    const Vec rhoTil = filt.apply(bestRho);
    Vec rhoBar(ne);
    for (int e = 0; e < ne; ++e) rhoBar(e) = heaviside(rhoTil(e), bestBeta, eta);
    const auto ss = adj.stressPNormGrad(rhoBar, sm);
    const Vec vmSolid = sm.vonMisesSolid(grid, ss.U);  // true (unrelaxed) sigma
    Vec vmCell(ne);
    for (int e = 0; e < ne; ++e)
        vmCell(e) = std::pow(std::clamp(rhoBar(e), 0.0, 1.0), sm.qRelax()) * vmSolid(e);

    Vec umag = Vec::Zero(nN);
    for (int n = 0; n < nN; ++n) {
        const double dx = ss.U(3 * n + 0), dy = ss.U(3 * n + 1),
                     dz = ss.U(3 * n + 2);
        umag(n) = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    const Vec tempC = nodalScalarToCell(grid, ss.T);
    const Vec umagC = nodalScalarToCell(grid, umag);

    std::filesystem::create_directories(s.output_dir);
    const std::string base = s.output_dir + "/" + s.name;
    VTKExporter::writeImageData(base + ".vti", grid,
                                {{"density", &rhoBar},
                                 {"vonMises", &vmCell},
                                 {"temperature", &tempC},
                                 {"displacement", &umagC}});

    int ng = 0;
    for (int e = 0; e < ne; ++e)
        if (rhoBar(e) > 0.1 && rhoBar(e) < 0.9) ++ng;
    const double gray = double(ng) / ne;
    std::printf(
        "\n==== %s (thermo-elastic) summary ====\n"
        "mass=%.4f  sigma_PN=%.4e  sigma_lim=%.4e  g1=%+.4f (%s)\n"
        "true max vonMises=%.4e  grey frac=%.3f  T range [%.3e, %.3e]\n"
        "wrote %s.vti\n",
        s.name.c_str(), rhoBar.mean(), ss.J, sigLim, ss.J / sigLim - 1.0,
        std::fabs(ss.J / sigLim - 1.0) < 0.05 ? "CONSTRAINT ACTIVE"
                                              : "constraint slack",
        vmSolid.maxCoeff(), gray, ss.T.minCoeff(), ss.T.maxCoeff(),
        base.c_str());
    return 0;
}

// ===========================================================================
// Fluid-thermal-elastic branch (CPU double, TripleAdjoint). Minimise compliance
// J = Fmech^T U through the Stokes-Brinkman -> CHT -> thermo-elastic cascade,
// s.t. m >= 1 constraints built from the JSON spec:
//   volume       g = mean(gamma)/max - 1        (fluid-volume budget)
//   tmax         g = J_T/max - 1                (ThermalObjectiveAdjoint,
//                                                p-norm wall temperature, P=8)
//   dissipation  g = Phi/max - 1                (DissipationAdjoint, 1/2 w^T H w)
//   vonmises     g = J_sigma/max - 1            (TripleAdjoint::solveStress,
//                                                qp-relaxed von Mises p-norm,
//                                                q=0.5, P=8, sigma on solid s=1-gamma)
// All adjoints are DF-validated gates; each gradient is chained through the
// SAME dH/drho_tilde .* (.) -> filter^T pipeline as the objective. With a
// single volume constraint this reproduces the v3 cooling_jacket run exactly.
// CONVENTION: gamma = 1 fluid, gamma = 0 solid (inverse of the v1/v2 density
// convention) -- consistent throughout the three adjoints and the constraints.
// ===========================================================================
int runFluidThermal(const ProblemSpec& s) {
    Grid3D grid(s.grid[0], s.grid[1], s.grid[2]);
    const int ne = grid.nElems();
    const int nN = grid.nNodes();

    // --- physics parameters from the spec. ---
    TripleAdjoint::Params prm;
    prm.mu = s.mu;
    prm.alphaStab = 1.0 / 12.0;
    prm.alphaMax = s.brinkman_max;   // Brinkman drag on solid cells (gamma=0)
    prm.alphaMin = 0.0;
    prm.qBrink = s.brinkman_q;
    prm.ks = s.k_solid;              // solid conductivity (gamma=0)
    prm.kf = s.k_fluid;              // fluid conductivity (gamma=1); advected heat
    prm.E0 = s.E0;
    prm.Emin = s.Emin;
    prm.p = s.penal;
    prm.alphaTh = s.alpha_th;
    prm.Tref = s.Tref;
    prm.nu = s.nu;

    // --- boundary conditions from the input language. ---
    // Stokes (4 DOF/node): no-slip walls / slip faces / pressure datum + drive.
    const std::vector<int> stokesFixed = BCResolver::stokesFixedDofs(grid, s);
    const std::array<double, 3> bodyForce = s.body_force;
    // Thermal: homogeneous Dirichlet (T=0) node set + nodal heat source Q.
    const std::vector<int> thermalNodes = BCResolver::thermalFixedNodes(grid, s);
    std::vector<std::uint8_t> dirMask(static_cast<size_t>(nN), 0);
    Vec dirVal = Vec::Zero(nN);
    for (int n : thermalNodes) dirMask[static_cast<size_t>(n)] = 1;
    const Vec Q = BCResolver::thermalSource(grid, s);
    // Elastic: fixed-DOF list + mechanical load vector (v1 resolvers).
    const auto fixedMask = BCResolver::fixedMask(grid, s);
    std::vector<int> elasticFixed;
    for (int d = 0; d < grid.nDof(); ++d)
        if (fixedMask[static_cast<size_t>(d)]) elasticFixed.push_back(d);
    const Vec Fmech = BCResolver::loadVector(grid, s);

    TripleAdjoint adj(grid, prm, stokesFixed, bodyForce, dirMask, dirVal, Q,
                      elasticFixed, Fmech);

    // --- constraint set from the input language (m >= 1). ---
    // Types: volume / tmax / dissipation / vonmises, all normalised
    // g = val/max - 1 <= 0. "vonmises" runs the full triple cascade a second
    // time per iteration through TripleAdjoint::solveStress (gate
    // test_vm_triple_fd, 8.0e-7) -- accepted demo cost.
    struct ConstraintDef { std::string type; double bound; };
    std::vector<ConstraintDef> cons;
    for (const auto& c : s.constraints) {
        if (c.type == "volume" || c.type == "tmax" || c.type == "dissipation" ||
            c.type == "vonmises") {
            if (c.max <= 0.0) {
                std::fprintf(stderr,
                             "fluid-thermal: constraint '%s' needs max > 0 "
                             "(got %g)\n", c.type.c_str(), c.max);
                return 1;
            }
            cons.push_back({c.type, c.max});
        } else {
            std::fprintf(stderr,
                         "fluid-thermal: unknown constraint type '%s' "
                         "(supported: volume, tmax, dissipation, vonmises)\n",
                         c.type.c_str());
            return 1;
        }
    }
    if (cons.empty()) cons.push_back({"volume", 0.5});  // legacy default
    const int m = static_cast<int>(cons.size());

    // Secondary-physics adjoints: instantiate ONCE (they precompute element
    // operators and reduced-DOF maps); one solve per constraint per iteration.
    // Same resolved BCs as TripleAdjoint; their Params are subsets of prm.
    // "vonmises" needs no extra object: TripleAdjoint::solveStress reuses adj.
    const TripleAdjoint::StressParams stressPrm;  // q=0.5, P=8 (gate defaults)
    std::unique_ptr<ThermalObjectiveAdjoint> tmaxAdj;
    std::unique_ptr<DissipationAdjoint> dissAdj;
    for (const auto& c : cons) {
        if (c.type == "tmax" && !tmaxAdj) {
            ThermalObjectiveAdjoint::Params tp;
            tp.mu = prm.mu; tp.alphaStab = prm.alphaStab;
            tp.alphaMax = prm.alphaMax; tp.alphaMin = prm.alphaMin;
            tp.qBrink = prm.qBrink; tp.ks = prm.ks; tp.kf = prm.kf;
            tmaxAdj = std::make_unique<ThermalObjectiveAdjoint>(
                grid, tp, stokesFixed, bodyForce, dirMask, dirVal, Q);
        } else if (c.type == "dissipation" && !dissAdj) {
            DissipationAdjoint::Params dp;
            dp.mu = prm.mu; dp.alphaStab = prm.alphaStab;
            dp.alphaMax = prm.alphaMax; dp.alphaMin = prm.alphaMin;
            dp.qBrink = prm.qBrink;
            // Body-force drive, homogeneous velocity Dirichlet -> zero lift.
            dissAdj = std::make_unique<DissipationAdjoint>(
                grid, dp, stokesFixed, bodyForce, Vec::Zero(4 * nN));
        }
    }

    // --- filter + Heaviside + MMA (cooling_jacket verbatim, m constraints). ---
    const double cell_mm = s.size_mm[0] / std::max(1, s.grid[0]);
    DensityFilter3D filt(grid, s.filter_radius_mm / cell_mm);
    const double eta = s.heaviside_eta;
    double vFrac = 0.5;  // rho seed = fluid budget when a volume constraint exists
    for (const auto& c : cons) if (c.type == "volume") vFrac = c.bound;

    MMAOptimizer::Params mp;
    mp.move = 0.2;  // conservative move limit for a stiff multiphysics objective
    MMAOptimizer mma(ne, m, mp);
    const Vec xmin = Vec::Constant(ne, 0.0), xmax = Vec::Ones(ne);
    Vec rho = Vec::Constant(ne, vFrac);  // start at the fluid budget

    std::printf(
        "topopt_run '%s' [fluid-thermal-elastic]: %dx%dx%d (%d elems, Stokes "
        "DOF=%d), obj=compliance, m=%d, %d iter\n",
        s.name.c_str(), s.grid[0], s.grid[1], s.grid[2], ne, 4 * nN, m,
        s.max_iter);
    std::printf("  constraints:");
    for (const auto& c : cons)
        std::printf(" %s<=%.4g", c.type.c_str(), c.bound);
    std::printf("\n");
    std::printf("  BCs: %zu Stokes fixed DOF, drive=[%.1f,%.1f,%.1f], %zu thermal "
                "Dirichlet nodes, |Q|=%.3e, %zu elastic fixed DOF, |Fmech|=%.3e\n",
                stokesFixed.size(), bodyForce[0], bodyForce[1], bodyForce[2],
                thermalNodes.size(), Q.norm(), elasticFixed.size(), Fmech.norm());
    std::printf("%4s %8s %10s %6s %8s", "it", "J", "J/Jref", "beta", "fluid");
    for (const auto& c : cons) std::printf(" %9.9s", ("g:" + c.type).c_str());
    std::printf(" %7s\n", "gray");

    double Jref = 0.0, J = 0.0, fluidFrac = 0.0, gray = 0.0, J_first = 0.0;
    Vec fvals(m), cRaw(m);  // normalised g_i and raw constraint values
    Eigen::MatrixXd dfdx(m, ne);
    for (int it = 1; it <= s.max_iter; ++it) {
        const double beta = betaAt(s, it);
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

        // Constraints g_i = val_i/max_i - 1 <= 0; gradients chained through the
        // SAME dH .* (.) -> filter^T pipeline as the objective.
        fluidFrac = gamma.mean();
        for (int i = 0; i < m; ++i) {
            const double bound = cons[static_cast<size_t>(i)].bound;
            const std::string& type = cons[static_cast<size_t>(i)].type;
            Vec dGdT(ne);
            if (type == "volume") {
                cRaw(i) = fluidFrac;
                for (int e = 0; e < ne; ++e)
                    dGdT(e) = (1.0 / ne) * dHdT(e) / bound;
            } else if (type == "tmax") {
                const auto st = tmaxAdj->solve(gamma);
                cRaw(i) = st.J;
                for (int e = 0; e < ne; ++e)
                    dGdT(e) = st.grad(e) * dHdT(e) / bound;
            } else if (type == "vonmises") {
                const auto sv = adj.solveStress(gamma, stressPrm);
                cRaw(i) = sv.J;
                for (int e = 0; e < ne; ++e)
                    dGdT(e) = sv.grad(e) * dHdT(e) / bound;
            } else {  // dissipation
                const auto sd = dissAdj->solve(gamma);
                cRaw(i) = sd.Phi;
                for (int e = 0; e < ne; ++e)
                    dGdT(e) = sd.grad(e) * dHdT(e) / bound;
            }
            fvals(i) = cRaw(i) / bound - 1.0;
            dfdx.row(i) = filt.applyT(dGdT).transpose();
        }

        rho = mma.step(rho, J / Jref, df0, fvals, dfdx, xmin, xmax);

        int ng = 0;
        for (int e = 0; e < ne; ++e)
            if (gamma(e) > 0.1 && gamma(e) < 0.9) ++ng;
        gray = double(ng) / ne;

        if (it == 1 || it % 5 == 0 || it == s.max_iter) {
            std::printf("%4d %8.3e %10.4f %6.0f %8.4f", it, J, J / Jref, beta,
                        fluidFrac);
            for (int i = 0; i < m; ++i) std::printf(" %+9.4f", fvals(i));
            std::printf(" %7.3f\n", gray);
        }
    }

    // --- final fields (recompute at final beta for reporting/export). ---
    const double betaF = betaAt(s, s.max_iter);
    const Vec rhoTil = filt.apply(rho);
    Vec gamma(ne);
    for (int e = 0; e < ne; ++e) gamma(e) = heaviside(rhoTil(e), betaF, eta);
    const auto sol = adj.solve(gamma);

    Vec speed = Vec::Zero(nN), umag = Vec::Zero(nN);
    for (int n = 0; n < nN; ++n) {
        const double ux = sol.w(4 * n + 0), uy = sol.w(4 * n + 1),
                     uz = sol.w(4 * n + 2);
        speed(n) = std::sqrt(ux * ux + uy * uy + uz * uz);
        const double dx = sol.U(3 * n + 0), dy = sol.U(3 * n + 1),
                     dz = sol.U(3 * n + 2);
        umag(n) = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    const Vec speedC = nodalScalarToCell(grid, speed);
    const Vec tempC = nodalScalarToCell(grid, sol.T);
    const Vec umagC = nodalScalarToCell(grid, umag);

    std::filesystem::create_directories(s.output_dir);
    const std::string base = s.output_dir + "/" + s.name;
    VTKExporter::writeImageData(base + ".vti", grid,
                                {{"density", &gamma},
                                 {"speed", &speedC},
                                 {"temperature", &tempC},
                                 {"displacement", &umagC}});

    // --- sanity metrics. ---
    fluidFrac = gamma.mean();
    int ng = 0;
    for (int e = 0; e < ne; ++e)
        if (gamma(e) > 0.1 && gamma(e) < 0.9) ++ng;
    gray = double(ng) / ne;

    const int nx = grid.nelx(), ny = grid.nely(), nz = grid.nelz();
    auto bandFluid = [&](int k0, int k1) {
        double sm = 0.0; int c = 0;
        for (int ez = k0; ez < k1; ++ez)
            for (int ey = 0; ey < ny; ++ey)
                for (int ex = 0; ex < nx; ++ex) { sm += gamma(grid.elemId(ex, ey, ez)); ++c; }
        return c > 0 ? sm / c : 0.0;
    };
    const int band = 3;
    const double fThroat = bandFluid(nz / 2 - band, nz / 2 + band);
    const double fEnds = 0.5 * (bandFluid(0, band) + bandFluid(nz - band, nz));

    std::printf("\n==== %s (fluid-thermal-elastic) summary ====\n", s.name.c_str());
    std::printf("J: first=%.4e  final=%.4e  (ratio %.3f)\n", J_first, sol.J,
                sol.J / (std::fabs(J_first) > 1e-30 ? J_first : 1.0));

    // Per-constraint report on the final design (forward-only re-evaluation).
    std::string activeSet;
    for (int i = 0; i < m; ++i) {
        const auto& c = cons[static_cast<size_t>(i)];
        double val = 0.0;
        if (c.type == "volume") val = fluidFrac;
        else if (c.type == "tmax") val = tmaxAdj->objective(gamma);
        else if (c.type == "vonmises") val = adj.stressObjective(gamma, stressPrm);
        else val = dissAdj->objective(gamma);
        const double g = val / c.bound - 1.0;
        const char* status = (g > 0.05)              ? "VIOLATED"
                             : (std::fabs(g) < 0.05) ? "ACTIVE"
                                                     : "slack";
        if (std::fabs(g) < 0.05) {
            if (!activeSet.empty()) activeSet += ", ";
            activeSet += c.type;
        }
        std::printf("constraint %-12s val=%.4e  max=%.4e  g=%+.4f -> %s\n",
                    c.type.c_str(), val, c.bound, g, status);
    }
    std::printf("active set: {%s}\n",
                activeSet.empty() ? "empty" : activeSet.c_str());
    std::printf("grey fraction (gamma in [0.1,0.9]) = %.3f\n", gray);
    std::printf("fluid density: throat=%.3f  ends=%.3f  ratio=%.2f -> %s\n",
                fThroat, fEnds, fThroat / std::max(fEnds, 1e-9),
                (fThroat > 1.1 * fEnds) ? "MORE FLUID AT THROAT"
                                        : "no clear throat concentration");
    std::printf("max coolant speed = %.4f   T range [%.4f, %.4f]\n",
                adj.maxSpeed(sol.w), sol.T.minCoeff(), sol.T.maxCoeff());
    std::printf("wrote %s.vti\n", base.c_str());
    return 0;
}

// ===========================================================================
// Axisymmetric structural branch (CPU double, AxiStressAdjoint): dim=="axi",
// physics=["elastic"]. Mass minimisation under a von Mises p-norm stress
// constraint on a body-fitted (r,z) mesh — the nozzle_profiled demonstrator
// driven from JSON. Loop logic reused VERBATIM from src/apps/nozzle_profiled.cpp
// (only the parameters come from the spec). Density convention rho=1 material.
//
// Schema conventions (documented choices):
//  - grid[0]=nr, grid[1]=nz, grid[2] ignored; H = size_mm[1].
//  - geometry "nozzle": bore r_in(z) = r_throat + K (z-H/2)^2 from domain.nozzle
//    {r_throat, K, wall}; band r_in..r_in+wall. size_mm[0]/[2] ignored.
//  - geometry "box": rectangular annulus a=size_mm[0], b=size_mm[2] (a>0: the
//    axis r=0 is never meshed).
//  - bc.fixed face "z-"/"z+" dof "z": plane-strain u_z=0 on that end row.
//  - bc.pressure face "inner" (alias "r-"): profiled bore pressure, Gaussian
//    bump peaked at the throat (nozzle_profiled profile: bump=3, sig=0.18 H);
//    value = PEAK pressure at the throat, i.e. p0 = value/(1+bump).
//  - constraints[type=vonmises]: max_rel (relative to the SOLID design's
//    sigma_PN, demonstrator convention) takes precedence over absolute max.
//  - filter.radius_mm is converted to cells with the axial cell size hz
//    (uniform even on the mapped grid): rmin = radius_mm / hz.
// ===========================================================================

// 2D density filter (linear hat, radius rmin cells) — nozzle_profiled verbatim.
struct DensityFilterAxi {
    const Grid2DAxi& g;
    double rmin;
    std::vector<std::vector<std::pair<int, double>>> wts;

    DensityFilterAxi(const Grid2DAxi& grid, double r) : g(grid), rmin(r) {
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

// Per-node radius map r(i,j) = r_in(z_j) + i * band/nr — nozzle_profiled verbatim.
std::vector<double> nozzleNodeRadii(const Grid2DAxi& g,
                                    const ProblemSpec::NozzleParams& nz,
                                    double H) {
    std::vector<double> rmap(static_cast<size_t>(g.nNodes()));
    for (int j = 0; j <= g.nz(); ++j) {
        const double z = g.z(j);
        const double d = z - 0.5 * H;
        const double ri = nz.r_throat + nz.K * d * d, ro = ri + nz.wall;
        for (int i = 0; i <= g.nr(); ++i)
            rmap[static_cast<size_t>(g.nodeId(i, j))] =
                ri + i * (ro - ri) / g.nr();
    }
    return rmap;
}

// Revolved (Pappus) element volume weight: r_centroid * planar area (shoelace).
Vec axiVolumeWeights(const Grid2DAxi& g) {
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

int runAxiStruct(const ProblemSpec& s) {
    const int nr = s.grid[0], nz = s.grid[1];
    const double H = s.size_mm[1];
    const bool isNozzle = (s.geometry == "nozzle");
    if (!isNozzle && s.geometry != "box") {
        std::fprintf(stderr, "axi: unsupported geometry '%s' (nozzle | box)\n",
                     s.geometry.c_str());
        return 1;
    }
    const double a = isNozzle ? s.nozzle.r_throat : s.size_mm[0];
    const double b = isNozzle ? s.nozzle.r_throat + s.nozzle.wall : s.size_mm[2];
    if (nr < 1 || nz < 1 || H <= 0.0 || a <= 0.0 || b <= a) {
        std::fprintf(stderr,
                     "axi: invalid domain (nr=%d nz=%d H=%g a=%g b=%g; need "
                     "nr,nz>=1, H>0, 0<a<b)\n", nr, nz, H, a, b);
        return 1;
    }
    if (s.objective != "mass") {
        std::fprintf(stderr, "axi: objective must be \"mass\" (got '%s')\n",
                     s.objective.c_str());
        return 1;
    }

    // Body-fitted axisymmetric mesh: rectangular index space, mapped radii.
    Grid2DAxi grid(nr, nz, a, b, H);
    if (isNozzle) grid.setNodeRadii(nozzleNodeRadii(grid, s.nozzle, H));

    // Wall contour (box: constant band) for the exports.
    auto rInAt = [&](double z) {
        if (!isNozzle) return a;
        const double d = z - 0.5 * H;
        return s.nozzle.r_throat + s.nozzle.K * d * d;
    };
    auto rOutAt = [&](double z) {
        return isNozzle ? rInAt(z) + s.nozzle.wall : b;
    };

    // --- BCs: direct resolution (no 2D BCResolver). Supported selectors only.
    // fixed: face "z-"/"z+" dof "z" -> plane-strain slice u_z=0 (hoop stiffness
    // makes K SPD, no radial pin needed — validated Lame BC family).
    std::vector<int> fixed;
    for (const auto& e : s.fixed) {
        if (e.dof == "z" && e.face == "z-") {
            for (int i = 0; i <= nr; ++i)
                fixed.push_back(2 * grid.nodeId(i, 0) + 1);
        } else if (e.dof == "z" && e.face == "z+") {
            for (int i = 0; i <= nr; ++i)
                fixed.push_back(2 * grid.nodeId(i, nz) + 1);
        } else {
            std::fprintf(stderr,
                         "axi: unsupported fixed selector (face='%s' edge='%s' "
                         "node='%s' region='%s' dof='%s'); supported: face "
                         "z-/z+ with dof z\n", e.face.c_str(), e.edge.c_str(),
                         e.node.c_str(), e.region.c_str(), e.dof.c_str());
            return 1;
        }
    }
    if (fixed.empty()) {
        std::fprintf(stderr, "axi: no fixed BC (need bc.fixed face z-/z+ dof z)\n");
        return 1;
    }

    // pressure: face "inner"/"r-" -> profiled bore pressure peaked at the
    // throat (nozzle_profiled profile). value = peak pressure at the throat.
    double pPeak = 0.0;
    bool havePressure = false;
    for (const auto& e : s.pressure) {
        if (e.face == "inner" || e.face == "r-") {
            pPeak = e.value;
            havePressure = true;
        } else {
            std::fprintf(stderr,
                         "axi: unsupported pressure selector (face='%s'); "
                         "supported: face inner (alias r-)\n", e.face.c_str());
            return 1;
        }
    }
    if (!havePressure || pPeak <= 0.0) {
        std::fprintf(stderr,
                     "axi: need bc.pressure face inner with value > 0 (peak)\n");
        return 1;
    }
    const double bump = 3.0, sig = 0.18 * H, p0 = pPeak / (1.0 + bump);
    std::vector<double> pRow(static_cast<size_t>(grid.nzn()));
    for (int j = 0; j <= nz; ++j) {
        const double t = (grid.z(j) - 0.5 * H) / sig;
        pRow[static_cast<size_t>(j)] = p0 * (1.0 + bump * std::exp(-0.5 * t * t));
    }
    FEM2DAxi femLoad(grid, s.nu);
    const Vec F = femLoad.pressureLoadInnerProfiled(pRow);

    // --- material + validated adjoint/stress machinery. ---
    AxiStressAdjoint::Material mat;
    mat.E0 = s.E0; mat.Emin = s.Emin; mat.p = s.penal; mat.nu = s.nu;
    AxiStressAdjoint adj(grid, mat, fixed, F);
    StressModelAxi sm(mat.nu, /*qRelax=*/0.5, /*Pagg=*/8.0);
    DensityFilterAxi filt(grid, s.filter_radius_mm / grid.hz());

    const int ne = grid.nElems();
    const Vec w = axiVolumeWeights(grid);
    const double wtot = w.sum();

    // Stress limit: max_rel is relative to the SOLID design's aggregate
    // (demonstrator convention, keeps the constraint active); max is absolute.
    double maxRel = 0.0, maxAbs = 0.0;
    bool haveVM = false;
    for (const auto& c : s.constraints)
        if (c.type == "vonmises") { maxRel = c.max_rel; maxAbs = c.max; haveVM = true; }
    if (!haveVM) {
        std::fprintf(stderr, "axi: missing constraints[type=vonmises]\n");
        return 1;
    }
    const Vec rhoSolid = Vec::Ones(ne);
    const double sigmaSolid = adj.stressPNorm(filt.apply(rhoSolid), sm);
    const double sigmaLim = (maxRel > 0.0) ? maxRel * sigmaSolid : maxAbs;
    if (sigmaLim <= 0.0) {
        std::fprintf(stderr,
                     "axi: constraints[type=vonmises] needs max_rel > 0 or "
                     "max > 0\n");
        return 1;
    }

    std::printf(
        "topopt_run '%s' [axi-elastic]: %dx%d (%d elems), geometry=%s, "
        "obj=mass, vonMises<=%.4f, %d iter\n",
        s.name.c_str(), nr, nz, ne, s.geometry.c_str(), sigmaLim, s.max_iter);
    if (isNozzle)
        std::printf("nozzle: throat r_in=%.2f, ends r_in=%.2f, wall band=%.2f\n",
                    rInAt(0.5 * H), rInAt(0.0), s.nozzle.wall);
    std::printf("  BCs: %zu fixed DOF, p_peak=%.4f (p0=%.4f), |F|=%.3e\n",
                fixed.size(), pPeak, p0, F.norm());
    std::printf("solid sigma_PN = %.4f  -> sigma_lim = %.4f\n", sigmaSolid,
                sigmaLim);

    // MMA: 1 design var per element, 1 stress constraint. Mass objective.
    // move=0.12 (nozzle_profiled): stress-constrained TO is stiff at high beta.
    MMAOptimizer::Params mp;
    mp.move = 0.12;
    MMAOptimizer mma(ne, 1, mp);
    const Vec xmin = Vec::Constant(ne, 1e-3), xmax = Vec::Ones(ne);
    Vec rho = Vec::Constant(ne, 0.6);
    const double eta = s.heaviside_eta;

    double mass = 0.0, gcon = 0.0, sigPN = 0.0;
    std::printf("%4s %8s %10s %6s %8s %6s\n", "it", "mass", "sigma_PN", "beta",
                "g", "gray");
    for (int it = 1; it <= s.max_iter; ++it) {
        const double beta = betaAt(s, it);
        const Vec rhoTil = filt.apply(rho);
        Vec gamma(ne), dHdT(ne);
        for (int e = 0; e < ne; ++e) {
            gamma(e) = heaviside(rhoTil(e), beta, eta);
            dHdT(e) = dHeaviside(rhoTil(e), beta, eta);
        }

        // Objective: normalised (Pappus-weighted) mass + chain-ruled gradient.
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
        Eigen::MatrixXd dfdx(1, ne); dfdx.row(0) = dg1.transpose();

        rho = mma.step(rho, mass, df0, fvals, dfdx, xmin, xmax);

        if (it == 1 || it % 5 == 0 || it == s.max_iter) {
            int ngi = 0;
            for (int e = 0; e < ne; ++e)
                if (gamma(e) > 0.1 && gamma(e) < 0.9) ++ngi;
            std::printf("%4d %8.4f %10.4f %6.0f %+8.4f %6.3f\n", it, mass, sigPN,
                        beta, gcon, double(ngi) / ne);
        }
    }

    // Final physical density at the last beta.
    const double betaF = betaAt(s, s.max_iter);
    const Vec rhoTil = filt.apply(rho);
    Vec gamma(ne);
    for (int e = 0; e < ne; ++e) gamma(e) = heaviside(rhoTil(e), betaF, eta);

    // Grey measure (fraction of intermediate cells) -> quasi-binary check.
    int ng = 0;
    for (int e = 0; e < ne; ++e)
        if (gamma(e) > 0.1 && gamma(e) < 0.9) ++ng;
    const double gray = double(ng) / ne;
    std::printf("done: mass=%.4f sigma_PN=%.4f g=%+.4f gray=%.3f\n", mass,
                sigPN, gcon, gray);

    // Wall-thickening diagnostic: mean filled radial fraction per z-band.
    if (nz >= 24) {
        auto filledFrac = [&](int j0, int j1) {
            double sfill = 0.0;
            for (int ej = j0; ej < j1; ++ej)
                for (int ei = 0; ei < nr; ++ei)
                    sfill += gamma(grid.elemId(ei, ej));
            return sfill / ((j1 - j0) * nr);
        };
        const double fThroat = filledFrac(nz / 2 - 6, nz / 2 + 6);
        const double fEnds = 0.5 * (filledFrac(0, 12) + filledFrac(nz - 12, nz));
        std::printf("filled wall fraction: throat=%.3f  ends=%.3f  ratio=%.2f "
                    "-> %s\n", fThroat, fEnds, fThroat / fEnds,
                    (fThroat > fEnds * 1.05) ? "THICKER AT THROAT (expected)"
                                             : "no clear throat reinforcement");
    }

    std::filesystem::create_directories(s.output_dir);
    const std::string base = s.output_dir + "/" + s.name;

    // --- Cross-section PNG (canonical orientation): full-diameter view, black
    // bore/void, white material, grey revolution-axis line at r=0 (outside the
    // meshed domain: the left/right borders are the OUTER wall, the material
    // starts at r_in(z) inboard — lesson: left edge of the half-view is the
    // inner wall r=a, NOT the axis). Top of image = z=H.
    {
        const double Rmax = rOutAt(0.0) > rOutAt(0.5 * H) ? rOutAt(0.0)
                                                          : rOutAt(0.5 * H);
        const int Wpix = 240, sc = 3, Hpix = nz * sc;
        const double dx = 2.0 * Rmax / Wpix;
        std::vector<unsigned char> img(static_cast<size_t>(Wpix) * Hpix, 0);
        for (int row = 0; row < Hpix; ++row) {
            const int ej = nz - 1 - row / sc;
            const double zc = grid.z(ej) + 0.5 * grid.hz();
            const double ri = rInAt(zc), ro = rOutAt(zc);
            for (int col = 0; col < Wpix; ++col) {
                const double x = -Rmax + (col + 0.5) * dx;
                const double rr = std::fabs(x);
                unsigned char px = 0;                    // void/outside -> black
                if (std::fabs(x) < 0.6 * dx) {
                    px = 110;                            // revolution axis (r=0)
                } else if (rr >= ri && rr < ro) {
                    int i = static_cast<int>((rr - ri) / (ro - ri) * nr);
                    i = std::clamp(i, 0, nr - 1);
                    const double v =
                        std::clamp(gamma(grid.elemId(i, ej)), 0.0, 1.0);
                    px = static_cast<unsigned char>(std::lround(255.0 * v));
                }
                img[static_cast<size_t>(row) * Wpix + col] = px;
            }
        }
        PngWriter::writeGray(base + ".png", Wpix, Hpix, img);
        std::printf("wrote %s.png (%dx%d; black=void/bore, white=material, "
                    "grey line=axis r=0)\n", base.c_str(), Wpix, Hpix);
    }

    // --- Revolved 3D density (voxelised) -> VTK + STL as the demonstrator. ---
    const bool wantVti =
        std::find(s.formats.begin(), s.formats.end(), "vti") != s.formats.end();
    const bool wantStl =
        std::find(s.formats.begin(), s.formats.end(), "stl") != s.formats.end();
    if (wantVti || wantStl) {
        const double Rmax = rOutAt(0.0) > rOutAt(0.5 * H) ? rOutAt(0.0)
                                                          : rOutAt(0.5 * H);
        const int nxy = 72, nz3 = nz;
        Grid3D g3(nxy, nxy, nz3);
        Vec rho3(g3.nElems());
        const double hxy = 2.0 * Rmax / nxy;
        for (int kz = 0; kz < nz3; ++kz) {
            const double zc = grid.z(kz) + 0.5 * grid.hz();
            const double ri = rInAt(zc), ro = rOutAt(zc);
            for (int jy = 0; jy < nxy; ++jy)
                for (int ix = 0; ix < nxy; ++ix) {
                    const double x = -Rmax + (ix + 0.5) * hxy;
                    const double y = -Rmax + (jy + 0.5) * hxy;
                    const double rr = std::sqrt(x * x + y * y);
                    double v = 0.0;
                    if (rr >= ri && rr < ro) {
                        const int i = std::clamp(
                            int((rr - ri) / (ro - ri) * nr), 0, nr - 1);
                        v = gamma(grid.elemId(i, kz));
                    }
                    rho3(g3.elemId(ix, jy, kz)) = v;
                }
        }
        if (wantVti) {
            VTKExporter::writeImageData(base + ".vti", g3, {{"density", &rho3}});
            std::printf("wrote %s.vti (revolved density, %d^2x%d voxels)\n",
                        base.c_str(), nxy, nz3);
        }
        if (wantStl) {
            STLExporter::writeVoxelSurface(base + ".stl", rho3, g3, s.stl_iso);
            std::printf("wrote %s.stl (revolved wall, %d^2x%d voxels)\n",
                        base.c_str(), nxy, nz3);
        }
    }
    return 0;
}

int runStructural(const ProblemSpec& s) {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    Grid3D grid(s.grid[0], s.grid[1], s.grid[2]);
    const int ne = grid.nElems();

    // Boundary conditions from the input language.
    const auto fixedMask = BCResolver::fixedMask(grid, s);
    const Vec F = BCResolver::loadVector(grid, s);

    MetalContext ctx;
    if (!ctx.valid()) { std::fprintf(stderr, "no Metal device\n"); pool->release(); return 1; }
    CGSolver3D solver(ctx, grid, H8Element::stiffness(s.nu));
    const double cell_mm = s.size_mm[0] / std::max(1, s.grid[0]);
    Helmholtz3D filter(ctx, grid, s.filter_radius_mm / cell_mm);
    if (!solver.valid() || !filter.valid()) {
        std::fprintf(stderr, "GPU init failed\n"); pool->release(); return 1;
    }
    const double volfrac = volumeConstraint(s);
    SIMP3D simp(SIMP3D::Params{s.E0, s.Emin, s.penal, volfrac, 0.2});
    const auto filterFn = [&](const Vec& x) { return filter.apply(x); };

    Vec rho = Vec::Constant(ne, volfrac);
    Vec rhoPhys = filterFn(rho);
    const Vec dv0 = filterFn(Vec::Ones(ne));

    std::vector<float> Ff(static_cast<size_t>(grid.nDof()));
    for (int d = 0; d < grid.nDof(); ++d) Ff[static_cast<size_t>(d)] = static_cast<float>(F(d));
    std::vector<float> Ef(static_cast<size_t>(ne)), Uf(static_cast<size_t>(grid.nDof())), cef;

    std::printf("topopt_run '%s': %dx%dx%d (%d elems), obj=%s vol<=%.2f, %d iter\n",
                s.name.c_str(), s.grid[0], s.grid[1], s.grid[2], ne,
                s.objective.c_str(), volfrac, s.max_iter);

    double change = 1.0, c = 0.0;
    for (int it = 1; it <= s.max_iter && change > 0.01; ++it) {
        const Vec E = simp.youngModulus(rhoPhys);
        for (int e = 0; e < ne; ++e) Ef[static_cast<size_t>(e)] = static_cast<float>(E(e));
        solver.solve(Ef, Ff, fixedMask, Uf, 4000, 1e-4f);
        solver.strainEnergy(cef);
        Vec ce(ne);
        c = 0.0;
        for (int e = 0; e < ne; ++e) {
            ce(e) = static_cast<double>(cef[static_cast<size_t>(e)]);
            c += E(e) * ce(e);
        }
        const Vec dc = filterFn(simp.complianceSensitivity(rhoPhys, ce));
        const SIMP3D::OCResult oc = simp.ocUpdate(rho, dc, dv0, filterFn);
        change = (oc.rho - rho).cwiseAbs().maxCoeff();
        rho = oc.rho;
        rhoPhys = oc.rhoPhys;
        if (it % 10 == 0 || it == 1)
            std::printf("  it %3d | C=%.4f | vol=%.4f | change=%.4f\n",
                        it, c, rhoPhys.mean(), change);
    }

    // Outputs (formats/dir from the spec).
    std::filesystem::create_directories(s.output_dir);
    const std::string base = s.output_dir + "/" + s.name;
    for (const auto& fmt : s.formats) {
        if (fmt == "vti") {
            VTKExporter::writeImageData(base + ".vti", grid, {{"density", &rhoPhys}});
        } else if (fmt == "stl") {
            if (s.stl_method == "marching_cubes") {
                std::vector<double> rhoElem(static_cast<size_t>(ne));
                for (int e = 0; e < ne; ++e) rhoElem[static_cast<size_t>(e)] = rhoPhys(e);
                const auto nodal = MarchingCubes::elementToNodal(rhoElem, grid);
                const auto tris = MarchingCubes::extract(nodal, grid, s.stl_iso);
                MarchingCubes::writeSTL(base + ".stl", tris);
            } else {
                STLExporter::writeVoxelSurface(base + ".stl", rhoPhys, grid, s.stl_iso);
            }
        }
    }
    std::printf("done: C=%.4f vol=%.4f -> %s.{%s}\n", c, rhoPhys.mean(), base.c_str(),
                s.formats.empty() ? "" : s.formats[0].c_str());
    pool->release();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <problem.topopt.json>\n", argv[0]);
        return 2;
    }
    ProblemSpec spec;
    try {
        spec = ProblemSpec::fromFile(argv[1]);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    // Physics dispatch. v1: structural (GPU). v2: thermo-elastic (CPU).
    auto has = [&](const char* p) {
        return std::find(spec.physics.begin(), spec.physics.end(), p) !=
               spec.physics.end();
    };
    const bool onlyElastic = spec.physics.size() == 1 && spec.physics[0] == "elastic";
    // Axisymmetric structural (profiled-nozzle demonstrator from JSON).
    if (spec.dim == "axi") {
        if (onlyElastic) return runAxiStruct(spec);
        std::fprintf(stderr,
                     "dim=axi supports physics=[\"elastic\"] only (got %s).\n",
                     spec.physics.empty() ? "none" : spec.physics[0].c_str());
        return 3;
    }
    if (spec.dim == "3d" && onlyElastic) return runStructural(spec);
    // v3 fluid-thermal-elastic: full Stokes-Brinkman -> CHT -> thermo-elastic.
    if (spec.dim == "3d" && has("fluid") && has("thermal") && has("elastic"))
        return runFluidThermal(spec);
    // v2 thermo-elastic: thermal + elastic (and no fluid).
    if (spec.dim == "3d" && has("thermal") && has("elastic") && !has("fluid"))
        return runThermoElastic(spec);
    std::fprintf(stderr,
                 "topopt_run supports dim=3d physics=[elastic] (v1), "
                 "[thermal,elastic] (v2), [fluid,thermal,elastic] (v3) and "
                 "dim=axi physics=[elastic]; got "
                 "dim=%s physics[0]=%s.\n",
                 spec.dim.c_str(), spec.physics.empty() ? "?" : spec.physics[0].c_str());
    return 3;
}
