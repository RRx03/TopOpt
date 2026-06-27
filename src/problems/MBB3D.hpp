#pragma once

#include <cstdint>
#include <vector>

#include <Eigen/Core>

#include "core/Grid3D.hpp"

namespace topopt {

// 3D MBB beam (half-domain by symmetry, extruded). Resolution-independent BC
// definition (defined by faces/edges), so it can be instantiated at any grid
// level of the multi-grid hierarchy.
//   x=0 face: ux=0 (symmetry)      z=0 face: uz=0 (symmetry)
//   bottom-right edge (x=L,y=0,all z): uy=0 (roller)
//   load: -y along top-left edge (x=0, y=H, all z), total = -1
inline void mbb3dBoundary(const Grid3D& g, std::vector<std::uint8_t>& fixedMask,
                          Eigen::VectorXd& F) {
    fixedMask.assign(static_cast<size_t>(g.nDof()), 0);
    auto fix = [&](int dof) { fixedMask[static_cast<size_t>(dof)] = 1; };

    for (int k = 0; k <= g.nelz(); ++k)
        for (int j = 0; j <= g.nely(); ++j)
            fix(3 * g.nodeId(0, j, k) + 0);            // x=0: ux=0
    for (int j = 0; j <= g.nely(); ++j)
        for (int i = 0; i <= g.nelx(); ++i)
            fix(3 * g.nodeId(i, j, 0) + 2);            // z=0: uz=0
    for (int k = 0; k <= g.nelz(); ++k)
        fix(3 * g.nodeId(g.nelx(), 0, k) + 1);         // bottom-right edge: uy=0

    F = Eigen::VectorXd::Zero(g.nDof());
    const double fz = -1.0 / (g.nelz() + 1);
    for (int k = 0; k <= g.nelz(); ++k)
        F(3 * g.nodeId(0, g.nely(), k) + 1) = fz;      // load top-left edge
}

} // namespace topopt
