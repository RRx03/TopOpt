// Input-language validation: a MBB ProblemSpec (JSON) must resolve to EXACTLY the
// same boundary conditions as the hardcoded mbb3dBoundary. Proves the JSON input
// language reproduces a known setup, decoupling problem definition from code.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/Grid3D.hpp"
#include "io/BCResolver.hpp"
#include "io/ProblemSpec.hpp"
#include "problems/MBB3D.hpp"

using namespace topopt;

int main() {
    Grid3D g(60, 20, 20);

    // Reference: the hardcoded MBB boundary conditions.
    std::vector<std::uint8_t> refMask;
    Eigen::VectorXd refF;
    mbb3dBoundary(g, refMask, refF);

    // From the input language.
    const ProblemSpec s = ProblemSpec::fromFile("examples/mbb3d.topopt.json");
    const auto specMask = BCResolver::fixedMask(g, s);
    const Eigen::VectorXd specF = BCResolver::loadVector(g, s);

    int fails = 0;

    // (a) parsed fields sanity.
    const bool metaOK = (s.name == "mbb3d") && (s.grid[0] == 60) &&
                        (s.grid[1] == 20) && (s.grid[2] == 20) &&
                        (s.objective == "compliance") && !s.constraints.empty() &&
                        (s.constraints[0].type == "volume") &&
                        std::fabs(s.constraints[0].max - 0.3) < 1e-12;
    std::printf("[a] parse: name=%s grid=%dx%dx%d obj=%s vol_max=%.2f -> %s\n",
                s.name.c_str(), s.grid[0], s.grid[1], s.grid[2], s.objective.c_str(),
                s.constraints.empty() ? 0.0 : s.constraints[0].max,
                metaOK ? "ok" : "FAIL");
    fails += metaOK ? 0 : 1;

    // (b) fixed-DOF mask identical to the hardcoded MBB.
    int maskDiff = 0;
    for (size_t d = 0; d < refMask.size(); ++d)
        if (refMask[d] != specMask[d]) ++maskDiff;
    std::printf("[b] fixed DOFs: %d fixed (ref), diff vs spec = %d -> %s\n",
                (int)std::count(refMask.begin(), refMask.end(), (std::uint8_t)1),
                maskDiff, maskDiff == 0 ? "IDENTICAL" : "FAIL");
    fails += (maskDiff == 0) ? 0 : 1;

    // (c) load vector identical (to round-off).
    const double loadErr = (specF - refF).cwiseAbs().maxCoeff();
    const double totRef = refF.sum(), totSpec = specF.sum();
    std::printf("[c] loads: total ref=%.6f spec=%.6f  max|dF|=%.2e -> %s\n",
                totRef, totSpec, loadErr, loadErr < 1e-12 ? "IDENTICAL" : "FAIL");
    fails += (loadErr < 1e-12) ? 0 : 1;

    std::printf("%s (%d failure(s))\n", fails == 0 ? "ALL PASS" : "FAIL", fails);
    return fails;
}
