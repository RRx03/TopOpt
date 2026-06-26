#pragma once

#include <array>
#include <cstdint>

namespace topopt {

// Structured 2D quad mesh with top88-compatible column-major node numbering.
//
// Node (col, row), 0-based, row 0 = top, col 0 = left:
//     node_id = col * (nely + 1) + row
// DOF per node n: x -> 2n, y -> 2n+1.
//
// Element (elx, ely) corner nodes:
//     tl = elx*(nely+1) + ely      (top-left)
//     bl = tl + 1                  (bottom-left)
//     tr = tl + (nely+1)           (top-right)
//     br = tr + 1                  (bottom-right)
// The 8 element DOFs are returned in top88 KE order:
//     [bl_x, bl_y, br_x, br_y, tr_x, tr_y, tl_x, tl_y]
class Grid2D {
public:
    Grid2D(int nelx, int nely);

    int nelx() const { return nelx_; }
    int nely() const { return nely_; }
    int nElems() const { return nelx_ * nely_; }
    int nNodes() const { return (nelx_ + 1) * (nely_ + 1); }
    int nDof() const { return 2 * nNodes(); }

    int nodeId(int col, int row) const { return col * (nely_ + 1) + row; }
    int elemId(int elx, int ely) const { return elx * nely_ + ely; }

    // Corner node ids of an element, order [bl, br, tr, tl].
    std::array<int, 4> elementNodes(int elx, int ely) const;

    // 8 DOF indices of an element, top88 KE order.
    std::array<int, 8> elementDofs(int elx, int ely) const;

private:
    int nelx_;
    int nely_;
};

} // namespace topopt
