#pragma once

#include <memory>

#include <Eigen/Core>

#include "core/Grid3D.hpp"
#include "filter/Helmholtz3D.hpp"

namespace topopt {

// Helmholtz density filter with the radius expressed in PHYSICAL units (mm),
// the Phase 3 mesh-independent convention (Lazarov-Sigmund 2011). Internally
// the cell-radius passed to the underlying Helmholtz3D is radius_mm / cellSize_mm,
// so refining the mesh (smaller cellSize) keeps the physical feature size fixed.
class HelmholtzFilterPhysical {
public:
    HelmholtzFilterPhysical(gpu::MetalContext& ctx, const Grid3D& grid,
                            double radius_mm, double cellSize_mm)
        : radiusCells_(radius_mm / cellSize_mm),
          filter_(std::make_unique<Helmholtz3D>(ctx, grid, radiusCells_)) {}

    bool valid() const { return filter_->valid(); }
    Eigen::VectorXd apply(const Eigen::VectorXd& xe) const {
        return filter_->apply(xe);
    }
    double radiusCells() const { return radiusCells_; }

private:
    double radiusCells_;
    std::unique_ptr<Helmholtz3D> filter_;
};

} // namespace topopt
