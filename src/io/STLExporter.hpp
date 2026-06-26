#pragma once

#include <string>

#include <Eigen/Core>

#include "core/Grid3D.hpp"

namespace topopt {

// Export the solid region (rhoPhys >= threshold) as a watertight binary STL
// of the voxel boundary: a face is emitted only where a solid cell meets
// empty space or the domain border. Robust and manifold; smooth marching-cubes
// extraction is deferred to Phase 3.
class STLExporter {
public:
    static bool writeVoxelSurface(const std::string& path,
                                  const Eigen::VectorXd& rhoPhys,
                                  const Grid3D& grid, double threshold = 0.5);
};

} // namespace topopt
