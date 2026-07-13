#include "core/Grid2D.hpp"

namespace topopt {

Grid2D::Grid2D(int nelx, int nely) : nelx_(nelx), nely_(nely) {}

std::array<int, 4> Grid2D::elementNodes(int elx, int ely) const {
    const int tl = elx * (nely_ + 1) + ely;
    const int bl = tl + 1;
    const int tr = tl + (nely_ + 1);
    const int br = tr + 1;
    return {bl, br, tr, tl};
}

std::array<int, 8> Grid2D::elementDofs(int elx, int ely) const {
    const auto n = elementNodes(elx, ely);
    return {2 * n[0], 2 * n[0] + 1,   // bl
            2 * n[1], 2 * n[1] + 1,   // br
            2 * n[2], 2 * n[2] + 1,   // tr
            2 * n[3], 2 * n[3] + 1};  // tl
}

} // namespace topopt
