#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <Eigen/Core>

#include "core/Grid3D.hpp"
#include "core/Grid3DMultiLevel.hpp"
#include "gpu/MetalContext.hpp"
#include "topopt/ContinuationPolicy.hpp"

namespace topopt {

// Multi-grid warm-start topology optimizer: optimise coarse -> fine, carrying
// the design across levels by (volume-conserving) density prolongation. Most TO
// iterations happen on cheap coarse grids; fine levels refine from a good start.
// Linear solves use the Phase 2 matrix-free CG + Jacobi at each level. The
// physical-radius Helmholtz filter (mm) gives mesh independence.
//
// The V-cycle multigrid preconditioner is an optional further optimisation
// (see PHASE_3_REPORT.md); warm-start alone delivers the bulk of the speedup.
class MultiGridOptimizer {
public:
    using Vec = Eigen::VectorXd;
    using BCBuilder = std::function<void(const Grid3D&, std::vector<std::uint8_t>&, Vec&)>;

    struct Params {
        double volfrac = 0.3;
        double filterRadius_mm = 1.5;
        double move = 0.2;
        double E0 = 1.0;
        double Emin = 1e-4;       // iterative float32 CG (cf. LL-006)
        double nu = 0.3;
        int itersPerLevel = 25;
        float cgTol = 1e-4f;
        int cgMaxIter = 4000;
        bool verbose = true;
    };

    struct Result {
        Vec rhoPhysFine;        // filtered design on the finest grid
        double finalCompliance = 0.0;
        double seconds = 0.0;
        bool ok = false;
    };

    MultiGridOptimizer(gpu::MetalContext& ctx, const Grid3DMultiLevel& mg,
                       const Params& p, const ContinuationPolicy& policy)
        : ctx_(ctx), mg_(mg), p_(p), policy_(policy) {}

    Result run(const BCBuilder& bc);

private:
    gpu::MetalContext& ctx_;
    const Grid3DMultiLevel& mg_;
    Params p_;
    ContinuationPolicy policy_;
};

} // namespace topopt
