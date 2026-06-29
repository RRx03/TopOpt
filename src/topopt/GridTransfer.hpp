#pragma once

#include <Eigen/Core>

#include "core/Grid3D.hpp"

namespace topopt {

// Inter-grid transfer of an element density field between factor-2 grids.
// Both operations are volume-conserving (they preserve the mean), as required
// for multi-grid warm-start (cf. LL-LIT-010).
//
//  - prolongateDensity: coarse -> fine by injection. Each coarse cell maps to
//    its 8 children with the same value. Mean preserved exactly.
//  - restrictDensity:   fine -> coarse by averaging the 8 children. Mean preserved.
//
// Round-trip restrict(prolongate(x)) == x exactly.
class GridTransfer {
public:
    using Vec = Eigen::VectorXd;

    // coarse (nElems) -> fine (nElems). fine must be the 2x refinement of coarse.
    static Vec prolongateDensity(const Grid3D& coarse, const Grid3D& fine,
                                 const Vec& rhoCoarse);

    // fine (nElems) -> coarse (nElems). coarse must be the 2x coarsening of fine.
    static Vec restrictDensity(const Grid3D& fine, const Grid3D& coarse,
                               const Vec& rhoFine);
};

} // namespace topopt
