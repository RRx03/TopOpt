#pragma once

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "core/Grid3D.hpp"

namespace topopt {

// Minimal VTK exporter: writes a structured ImageData (.vti, XML ASCII) with one
// or more per-cell scalar fields on a uniform 3D grid. Opens directly in
// ParaView (3D rendering, colormaps, slices, iso-surfaces) — no dependency, no
// custom render engine. The standard postprocessing path for the whole project
// until a dedicated viewer is warranted (Phase 6 industrialisation).
class VTKExporter {
public:
    struct Field {
        std::string name;
        const Eigen::VectorXd* data;  // length = grid.nElems(), cell data
    };

    // Write cell-data fields on a uniform grid (unit spacing by default).
    static bool writeImageData(const std::string& path, const Grid3D& grid,
                               const std::vector<Field>& fields,
                               double spacing = 1.0) {
        std::ofstream f(path);
        if (!f) {
            std::fprintf(stderr, "VTKExporter: cannot open %s\n", path.c_str());
            return false;
        }
        const int nx = grid.nelx(), ny = grid.nely(), nz = grid.nelz();
        f << "<?xml version=\"1.0\"?>\n";
        f << "<VTKFile type=\"ImageData\" version=\"1.0\" "
             "byte_order=\"LittleEndian\">\n";
        f << "  <ImageData WholeExtent=\"0 " << nx << " 0 " << ny << " 0 " << nz
          << "\" Origin=\"0 0 0\" Spacing=\"" << spacing << " " << spacing << " "
          << spacing << "\">\n";
        f << "    <Piece Extent=\"0 " << nx << " 0 " << ny << " 0 " << nz << "\">\n";
        f << "      <CellData Scalars=\"" << (fields.empty() ? "" : fields[0].name)
          << "\">\n";
        for (const Field& fld : fields) {
            f << "        <DataArray type=\"Float32\" Name=\"" << fld.name
              << "\" format=\"ascii\">\n          ";
            // VTK cell order is x-fastest, then y, then z — matches elemId.
            for (int ez = 0; ez < nz; ++ez)
                for (int ey = 0; ey < ny; ++ey)
                    for (int ex = 0; ex < nx; ++ex)
                        f << static_cast<float>((*fld.data)(grid.elemId(ex, ey, ez)))
                          << ' ';
            f << "\n        </DataArray>\n";
        }
        f << "      </CellData>\n    </Piece>\n  </ImageData>\n</VTKFile>\n";
        std::printf("VTK: %zu field(s) on %dx%dx%d -> %s\n", fields.size(), nx, ny,
                    nz, path.c_str());
        return true;
    }
};

} // namespace topopt
