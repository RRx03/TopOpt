#include "io/PNGWriter.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace topopt {

bool PNGWriter::writeDensity(const std::string& path,
                             const Eigen::VectorXd& rho, int nelx, int nely) {
    std::vector<std::uint8_t> img(static_cast<size_t>(nelx) * nely);
    for (int elx = 0; elx < nelx; ++elx) {
        for (int ely = 0; ely < nely; ++ely) {
            const double r = std::clamp(rho(elx * nely + ely), 0.0, 1.0);
            const auto pixel = static_cast<std::uint8_t>((1.0 - r) * 255.0 + 0.5);
            img[static_cast<size_t>(ely) * nelx + elx] = pixel;
        }
    }
    const int stride = nelx;  // 1 byte/pixel
    return stbi_write_png(path.c_str(), nelx, nely, 1, img.data(), stride) != 0;
}

} // namespace topopt
