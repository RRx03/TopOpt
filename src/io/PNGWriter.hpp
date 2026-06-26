#pragma once

#include <string>

#include <Eigen/Core>

namespace topopt {

// Write a density field as an 8-bit grayscale PNG (one pixel per element).
// Convention: material (rho=1) -> black, void (rho=0) -> white.
// Element layout follows Grid2D: elemId = elx*nely + ely, row 0 = top.
class PNGWriter {
public:
    static bool writeDensity(const std::string& path,
                             const Eigen::VectorXd& rho, int nelx, int nely);
};

} // namespace topopt
