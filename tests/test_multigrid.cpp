// Inter-grid transfer validation (CPU, no Metal): density prolongation /
// restriction round-trip and volume conservation, plus hierarchy structure.
#include <cmath>
#include <cstdio>
#include <random>

#include "core/Grid3D.hpp"
#include "core/Grid3DMultiLevel.hpp"
#include "topopt/GridTransfer.hpp"

using namespace topopt;
using Vec = Eigen::VectorXd;

namespace {

// (a) round-trip restrict(prolongate(x)) == x exactly.
bool testRoundTrip() {
    Grid3D coarse(8, 6, 4), fine(16, 12, 8);
    std::mt19937 rng(123u);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    Vec x(coarse.nElems());
    for (int e = 0; e < coarse.nElems(); ++e) x(e) = dist(rng);

    const Vec up = GridTransfer::prolongateDensity(coarse, fine, x);
    const Vec back = GridTransfer::restrictDensity(fine, coarse, up);
    const double err = (back - x).cwiseAbs().maxCoeff();
    std::printf("[a] round-trip restrict.prolongate: maxerr=%.2e\n", err);
    return err < 1e-14;
}

// (b) volume (mean) conservation in both directions.
bool testVolumeConservation() {
    Grid3D coarse(8, 6, 4), fine(16, 12, 8);
    std::mt19937 rng(7u);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    Vec xc(coarse.nElems());
    for (int e = 0; e < coarse.nElems(); ++e) xc(e) = dist(rng);
    Vec xf(fine.nElems());
    for (int e = 0; e < fine.nElems(); ++e) xf(e) = dist(rng);

    const double eUp = std::fabs(
        GridTransfer::prolongateDensity(coarse, fine, xc).mean() - xc.mean());
    const double eDn = std::fabs(
        GridTransfer::restrictDensity(fine, coarse, xf).mean() - xf.mean());
    std::printf("[b] volume conservation: prolong dmean=%.2e  restrict dmean=%.2e\n",
                eUp, eDn);
    return eUp < 1e-14 && eDn < 1e-14;
}

// (c) hierarchy structure: dims halve, divisibility helper.
bool testHierarchy() {
    if (!Grid3DMultiLevel::divisible(128, 128, 128, 4)) return false;
    Grid3DMultiLevel mg(128, 128, 128, 4, 64.0);  // 64 mm domain
    bool ok = mg.nLevels() == 4 &&
              mg.coarsest().nelx() == 16 && mg.finest().nelx() == 128;
    // cell size: finest 64/128 = 0.5 mm, coarsest 0.5*8 = 4 mm.
    ok = ok && std::fabs(mg.cellSize(mg.nLevels() - 1) - 0.5) < 1e-12 &&
         std::fabs(mg.cellSize(0) - 4.0) < 1e-12;
    std::printf("[c] hierarchy: levels=%d coarsest nelx=%d finest nelx=%d "
                "h_fine=%.3f h_coarse=%.3f\n",
                mg.nLevels(), mg.coarsest().nelx(), mg.finest().nelx(),
                mg.cellSize(mg.nLevels() - 1), mg.cellSize(0));
    return ok;
}

} // namespace

int main() {
    int fails = 0;
    fails += testRoundTrip() ? 0 : 1;
    fails += testVolumeConservation() ? 0 : 1;
    fails += testHierarchy() ? 0 : 1;
    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    return fails;
}
