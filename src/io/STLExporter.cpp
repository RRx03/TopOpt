#include "io/STLExporter.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

namespace topopt {

namespace {

// A unit cube cell at integer corner (x,y,z) spans [x,x+1]x[y,y+1]x[z,z+1].
// Six faces with outward normals; each face = 2 triangles (CCW outward).
struct Tri {
    float n[3];
    float v[3][3];
};

void emitFace(std::vector<Tri>& tris, double x, double y, double z, int face) {
    // Corner offsets per face (CCW seen from outside), and the normal.
    // face: 0=-x,1=+x,2=-y,3=+y,4=-z,5=+z
    static const float N[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                                  {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    // Quad corners (4) for each face as (dx,dy,dz) in {0,1}.
    static const int Q[6][4][3] = {
        {{0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}},  // -x
        {{1, 0, 0}, {1, 0, 1}, {1, 1, 1}, {1, 1, 0}},  // +x
        {{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}},  // -y
        {{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}},  // +y
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},  // -z
        {{0, 0, 1}, {0, 1, 1}, {1, 1, 1}, {1, 0, 1}},  // +z
    };
    auto corner = [&](int c, float out[3]) {
        out[0] = static_cast<float>(x) + static_cast<float>(Q[face][c][0]);
        out[1] = static_cast<float>(y) + static_cast<float>(Q[face][c][1]);
        out[2] = static_cast<float>(z) + static_cast<float>(Q[face][c][2]);
    };
    // Triangles (0,1,2) and (0,2,3).
    for (int t = 0; t < 2; ++t) {
        Tri tri;
        for (int d = 0; d < 3; ++d) tri.n[d] = N[face][d];
        const int idx[3] = {0, t == 0 ? 1 : 2, t == 0 ? 2 : 3};
        for (int c = 0; c < 3; ++c) corner(idx[c], tri.v[c]);
        tris.push_back(tri);
    }
}

} // namespace

bool STLExporter::writeVoxelSurface(const std::string& path,
                                    const Eigen::VectorXd& rhoPhys,
                                    const Grid3D& grid, double threshold) {
    const int nx = grid.nelx(), ny = grid.nely(), nz = grid.nelz();
    auto solid = [&](int ex, int ey, int ez) -> bool {
        if (ex < 0 || ex >= nx || ey < 0 || ey >= ny || ez < 0 || ez >= nz)
            return false;
        return rhoPhys(grid.elemId(ex, ey, ez)) >= threshold;
    };

    std::vector<Tri> tris;
    for (int ez = 0; ez < nz; ++ez)
        for (int ey = 0; ey < ny; ++ey)
            for (int ex = 0; ex < nx; ++ex) {
                if (!solid(ex, ey, ez)) continue;
                const double x = ex, y = ey, z = ez;
                if (!solid(ex - 1, ey, ez)) emitFace(tris, x, y, z, 0);
                if (!solid(ex + 1, ey, ez)) emitFace(tris, x, y, z, 1);
                if (!solid(ex, ey - 1, ez)) emitFace(tris, x, y, z, 2);
                if (!solid(ex, ey + 1, ez)) emitFace(tris, x, y, z, 3);
                if (!solid(ex, ey, ez - 1)) emitFace(tris, x, y, z, 4);
                if (!solid(ex, ey, ez + 1)) emitFace(tris, x, y, z, 5);
            }

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "STLExporter: cannot open %s\n", path.c_str());
        return false;
    }
    char header[80] = {0};
    f.write(header, 80);
    const std::uint32_t ntri = static_cast<std::uint32_t>(tris.size());
    f.write(reinterpret_cast<const char*>(&ntri), 4);
    for (const Tri& t : tris) {
        f.write(reinterpret_cast<const char*>(t.n), 12);
        f.write(reinterpret_cast<const char*>(t.v), 36);
        const std::uint16_t attr = 0;
        f.write(reinterpret_cast<const char*>(&attr), 2);
    }
    std::printf("STL: %u triangles -> %s\n", ntri, path.c_str());
    return true;
}

} // namespace topopt
