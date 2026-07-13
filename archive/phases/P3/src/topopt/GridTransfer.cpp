#include "topopt/GridTransfer.hpp"

namespace topopt {

GridTransfer::Vec GridTransfer::prolongateDensity(const Grid3D& coarse,
                                                  const Grid3D& fine,
                                                  const Vec& rhoCoarse) {
    Vec rhoFine(fine.nElems());
    for (int ez = 0; ez < fine.nelz(); ++ez)
        for (int ey = 0; ey < fine.nely(); ++ey)
            for (int ex = 0; ex < fine.nelx(); ++ex) {
                const int cx = ex / 2, cy = ey / 2, cz = ez / 2;  // parent cell
                rhoFine(fine.elemId(ex, ey, ez)) =
                    rhoCoarse(coarse.elemId(cx, cy, cz));
            }
    return rhoFine;
}

GridTransfer::Vec GridTransfer::restrictDensity(const Grid3D& fine,
                                                const Grid3D& coarse,
                                                const Vec& rhoFine) {
    Vec rhoCoarse = Vec::Zero(coarse.nElems());
    for (int cz = 0; cz < coarse.nelz(); ++cz)
        for (int cy = 0; cy < coarse.nely(); ++cy)
            for (int cx = 0; cx < coarse.nelx(); ++cx) {
                double s = 0.0;
                for (int dz = 0; dz < 2; ++dz)
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                            s += rhoFine(fine.elemId(2 * cx + dx, 2 * cy + dy,
                                                     2 * cz + dz));
                rhoCoarse(coarse.elemId(cx, cy, cz)) = s / 8.0;
            }
    return rhoCoarse;
}

} // namespace topopt
