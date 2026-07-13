#pragma once

#include <string>
#include <vector>

#include <Eigen/Core>

#include "core/Grid3D.hpp"

namespace topopt {

// One output triangle: three vertices (physical coords) + an outward unit normal.
struct MCTriangle {
    Eigen::Vector3d v[3];
    Eigen::Vector3d n;  // outward unit normal (points toward decreasing field)
};

// Classic marching cubes (Lorensen-Cline, Paul Bourke's canonical 256-case
// tables) on a structured Grid3D with a scalar field sampled at the NODES.
//
// Iso-surface {f = iso}. For each cell the 8 corner values give an 8-bit index;
// triangle vertices are linearly interpolated along the 12 cube edges
// (t = (iso-f0)/(f1-f0)). Vertices on a shared cell edge are interpolated from
// the same two nodal values -> the surface is watertight (crack-free).
//
// Orientation: each triangle's winding is chosen so its geometric normal aligns
// with -grad(f), i.e. it points from the solid region (f > iso) toward the void
// (f < iso). Outward normals are therefore consistent across the whole surface.
class MarchingCubes {
public:
    // Extract the iso-surface. Physical coord of node (i,j,k) = origin + spacing*(i,j,k).
    static std::vector<MCTriangle> extract(
        const std::vector<double>& nodalField, const Grid3D& grid, double iso,
        double spacing = 1.0,
        const Eigen::Vector3d& origin = Eigen::Vector3d::Zero());

    // Element field -> nodal field (average of incident elements). Use when the
    // TO density lives on elements rather than nodes.
    static std::vector<double> elementToNodal(
        const std::vector<double>& elemField, const Grid3D& grid);

    // Write triangles as a binary STL (same layout as STLExporter).
    static bool writeSTL(const std::string& path,
                         const std::vector<MCTriangle>& tris);
};

} // namespace topopt
