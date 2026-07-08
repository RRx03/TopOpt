// CPU marching-cubes validation (no Metal), double precision.
//
//  ORACLE — sphere SDF. Nodal field f(x) = R − |x − c| sampled on a grid that
//  bounds the sphere (R = 0.35, c = centre of [0,1]³), iso = 0. The extracted
//  iso-surface must approximate the sphere:
//      area   ≈ 4πR²          (sum of triangle areas)
//      volume ≈ (4/3)πR³      (sum of signed tets  (1/6) v0·(v1×v2))
//  PASS if both are within a few % at nx = 64 AND the error decreases from
//  nx = 32 to nx = 64 (discretisation convergence).
//
//  WATERTIGHT — every interior edge is shared by exactly two triangles, so the
//  count of boundary edges (used by ≠ 2 triangles) must be 0.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include <Eigen/Geometry>

#include "core/Grid3D.hpp"
#include "io/MarchingCubes.hpp"

using namespace topopt;

namespace {

constexpr double kR = 0.35;
constexpr double kPi = 3.14159265358979323846;

struct Measure {
    double area;
    double volume;
    long boundaryEdges;
    long ntri;
};

Measure runSphere(int nx) {
    Grid3D grid(nx, nx, nx);
    const double h = 1.0 / nx;
    const Eigen::Vector3d c(0.5, 0.5, 0.5);

    std::vector<double> field(static_cast<size_t>(grid.nNodes()));
    for (int k = 0; k <= nx; ++k)
        for (int j = 0; j <= nx; ++j)
            for (int i = 0; i <= nx; ++i) {
                const Eigen::Vector3d p(i * h, j * h, k * h);
                field[static_cast<size_t>(grid.nodeId(i, j, k))] =
                    kR - (p - c).norm();
            }

    const auto tris = MarchingCubes::extract(field, grid, 0.0, h);

    double area = 0.0, vol = 0.0;
    for (const auto& t : tris) {
        area += 0.5 * (t.v[1] - t.v[0]).cross(t.v[2] - t.v[0]).norm();
        vol += t.v[0].dot(t.v[1].cross(t.v[2])) / 6.0;
    }

    // Watertight: count how many triangles use each undirected edge.
    // Quantise vertices (FP-safe, well below true vertex spacing).
    const double q = h * 1e-7;
    auto key = [&](const Eigen::Vector3d& v) {
        return std::array<long long, 3>{
            std::llround(v.x() / q), std::llround(v.y() / q),
            std::llround(v.z() / q)};
    };
    using K = std::array<long long, 3>;
    std::map<std::pair<K, K>, int> edgeCount;
    auto addEdge = [&](const K& a, const K& b) {
        edgeCount[a < b ? std::make_pair(a, b) : std::make_pair(b, a)] += 1;
    };
    for (const auto& t : tris) {
        const K a = key(t.v[0]), b = key(t.v[1]), c2 = key(t.v[2]);
        addEdge(a, b);
        addEdge(b, c2);
        addEdge(c2, a);
    }
    long boundary = 0;
    for (const auto& [e, n] : edgeCount)
        if (n != 2) ++boundary;

    return {area, std::fabs(vol), boundary,
            static_cast<long>(tris.size())};
}

bool report(const Measure& m, int nx, double& areaErr, double& volErr) {
    const double areaExact = 4.0 * kPi * kR * kR;
    const double volExact = 4.0 / 3.0 * kPi * kR * kR * kR;
    areaErr = std::fabs(m.area - areaExact) / areaExact;
    volErr = std::fabs(m.volume - volExact) / volExact;
    std::printf(
        "nx=%2d  tris=%5ld  area=%.5f (err %.3f%%)  vol=%.5f (err %.3f%%)  "
        "boundaryEdges=%ld\n",
        nx, m.ntri, m.area, 100.0 * areaErr, m.volume, 100.0 * volErr,
        m.boundaryEdges);
    return true;
}

} // namespace

int main() {
    int fails = 0;

    const Measure m32 = runSphere(32);
    const Measure m64 = runSphere(64);
    double a32, v32, a64, v64;
    report(m32, 32, a32, v32);
    report(m64, 64, a64, v64);

    // Accuracy at nx=64.
    const bool accOk = a64 < 0.03 && v64 < 0.03;
    std::printf("  accuracy@64 (area<3%%, vol<3%%) = %d\n", accOk);
    // Convergence 32 -> 64.
    const bool convOk = a64 < a32 && v64 < v32;
    std::printf("  convergence (err decreases 32->64): area %.3f%%->%.3f%%, "
                "vol %.3f%%->%.3f%%  = %d\n",
                100.0 * a32, 100.0 * a64, 100.0 * v32, 100.0 * v64, convOk);
    // Watertight (both resolutions).
    const bool tightOk = m32.boundaryEdges == 0 && m64.boundaryEdges == 0;
    std::printf("  watertight (boundary edges == 0) = %d\n", tightOk);

    fails += accOk ? 0 : 1;
    fails += convOk ? 0 : 1;
    fails += tightOk ? 0 : 1;

    // Optional: emit a smooth STL for visual comparison vs the voxel surface.
    {
        const int nx = 64;
        Grid3D grid(nx, nx, nx);
        const double h = 1.0 / nx;
        const Eigen::Vector3d c(0.5, 0.5, 0.5);
        std::vector<double> field(static_cast<size_t>(grid.nNodes()));
        for (int k = 0; k <= nx; ++k)
            for (int j = 0; j <= nx; ++j)
                for (int i = 0; i <= nx; ++i) {
                    const Eigen::Vector3d p(i * h, j * h, k * h);
                    field[static_cast<size_t>(grid.nodeId(i, j, k))] =
                        kR - (p - c).norm();
                }
        const auto tris = MarchingCubes::extract(field, grid, 0.0, h);
        MarchingCubes::writeSTL("output/mc_sphere.stl", tris);
    }

    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    return fails;
}
