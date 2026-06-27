#pragma once

#include <vector>

#include "core/Grid3D.hpp"

namespace topopt {

// Hierarchy of structured H8 grids related by factor-2 refinement, from the
// coarsest (level 0) to the finest. Each finer level doubles the element count
// per axis. The physical cell size halves each level (used by the physical
// Helmholtz filter and for mesh-independence).
//
// Built from the FINEST dimensions: they must be divisible by 2^(nLevels-1).
class Grid3DMultiLevel {
public:
    // domainLengthX_mm sets the physical scale; cell size at the finest level is
    // domainLengthX_mm / nelxFine. nLevels includes the finest (>=1).
    Grid3DMultiLevel(int nelxFine, int nelyFine, int nelzFine, int nLevels,
                     double domainLengthX_mm = 0.0) {
        int fx = nelxFine, fy = nelyFine, fz = nelzFine;
        // Build coarse->fine; compute coarse dims by halving the fine ones.
        std::vector<Grid3D> tmp;
        std::vector<double> htmp;
        const double hFine = (domainLengthX_mm > 0.0)
                                 ? domainLengthX_mm / nelxFine
                                 : 1.0;  // 1 cell unit if no physical scale
        for (int l = 0; l < nLevels; ++l) {
            tmp.emplace_back(fx, fy, fz);
            htmp.push_back(hFine * static_cast<double>(1 << l));  // coarser = larger h
            if (l + 1 < nLevels) {
                fx /= 2;
                fy /= 2;
                fz /= 2;
            }
        }
        // Reverse so index 0 = coarsest.
        for (int l = nLevels - 1; l >= 0; --l) {
            levels_.push_back(tmp[static_cast<size_t>(l)]);
            cellSize_.push_back(htmp[static_cast<size_t>(l)]);
        }
    }

    int nLevels() const { return static_cast<int>(levels_.size()); }
    const Grid3D& level(int l) const { return levels_[static_cast<size_t>(l)]; }
    const Grid3D& coarsest() const { return levels_.front(); }
    const Grid3D& finest() const { return levels_.back(); }
    double cellSize(int l) const { return cellSize_[static_cast<size_t>(l)]; }

    // True if the finest dims divide cleanly by 2^(nLevels-1).
    static bool divisible(int nx, int ny, int nz, int nLevels) {
        const int f = 1 << (nLevels - 1);
        return nx % f == 0 && ny % f == 0 && nz % f == 0;
    }

private:
    std::vector<Grid3D> levels_;
    std::vector<double> cellSize_;
};

} // namespace topopt
