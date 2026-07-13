#pragma once

#include <vector>

namespace topopt {

// Continuation parameters resolved per (level, local iteration). Today only the
// SIMP penalisation p; deliberately a struct so Phase 4-5 continuation params
// (stress eps-relaxation / qp, Brinkman alpha_max, Heaviside beta) slot in here
// without changing the optimizer interface.
struct ContinuationParams {
    double penal = 3.0;
    // Phase 4+: double epsRelax, alphaMax, heavisideBeta; ...
};

// Policy controlling how the SIMP penalisation evolves across the multi-grid
// hierarchy. The part designer chooses the trade-off between topological freedom
// at fine levels (needed for late-emerging fine features, e.g. cooling channels)
// and cost.
//
//  - Inherit  : full continuation 1->penalMax on the COARSEST level; finer
//               levels run at penalMax (cheap, standard large-scale approach).
//  - Restart  : full continuation 1->penalMax on EVERY level (max topological
//               freedom, expensive — use when fine features must nucleate late).
//  - Custom   : caller supplies the final penal per level; within a level the
//               value ramps from penalStart to that target.
//
// NOTE (coupling): topological freedom from continuation only lets the optimiser
// nucleate a fine feature; whether the feature can EXIST is set by the physical
// filter radius (mm). For thin cooling channels: small mm radius AND fine-level
// freedom (Restart, or Custom with low coarse / high fine penal) are both needed.
class ContinuationPolicy {
public:
    enum class Mode { Inherit, Restart, Custom };

    explicit ContinuationPolicy(Mode mode = Mode::Inherit, double penalMax = 3.0,
                                double penalStart = 1.0, int rampIters = 30)
        : mode_(mode), penalMax_(penalMax), penalStart_(penalStart),
          rampIters_(rampIters) {}

    // Custom: one final penal target per level (index 0 = coarsest).
    void setCustomLevelPenal(const std::vector<double>& perLevelPenal) {
        customPenal_ = perLevelPenal;
        mode_ = Mode::Custom;
    }

    Mode mode() const { return mode_; }

    // Resolve parameters at a given level (0=coarsest) and local iteration.
    ContinuationParams at(int level, int nLevels, int localIter) const {
        ContinuationParams p;
        const bool coarsest = (level == 0);
        switch (mode_) {
            case Mode::Inherit:
                p.penal = coarsest ? ramp(localIter, penalMax_) : penalMax_;
                break;
            case Mode::Restart:
                p.penal = ramp(localIter, penalMax_);
                break;
            case Mode::Custom: {
                const double target =
                    (level < static_cast<int>(customPenal_.size()))
                        ? customPenal_[static_cast<size_t>(level)]
                        : penalMax_;
                p.penal = ramp(localIter, target);
                break;
            }
        }
        (void)nLevels;
        return p;
    }

private:
    // Linear ramp penalStart -> target over rampIters, then hold target.
    double ramp(int localIter, double target) const {
        if (rampIters_ <= 1 || target <= penalStart_) return target;
        const double t = static_cast<double>(localIter) /
                         static_cast<double>(rampIters_);
        const double v = penalStart_ + (target - penalStart_) * (t < 1.0 ? t : 1.0);
        return v;
    }

    Mode mode_;
    double penalMax_;
    double penalStart_;
    int rampIters_;
    std::vector<double> customPenal_;
};

} // namespace topopt
