#pragma once

#include <array>
#include <cstdint>

namespace topopt {

// Structured 3D hexahedral mesh, unit-cube cells, row-major numbering.
//
// Node (i,j,k), 0-based, i along x, j along y, k along z:
//     node_id = i + j*nx + k*nx*ny      with nx = nelx+1, etc.
// DOF per node n: x -> 3n, y -> 3n+1, z -> 3n+2.
//
// Element (ex,ey,ez), local corner l = di + 2*dj + 4*dk (di,dj,dk in {0,1}):
//     global node = node_id(ex+di, ey+dj, ez+dk)
// The 24 element DOFs are [n0_x,n0_y,n0_z, n1_x,..., n7_z] in local node order.
class Grid3D {
public:
    Grid3D(int nelx, int nely, int nelz)
        : nelx_(nelx), nely_(nely), nelz_(nelz) {}

    int nelx() const { return nelx_; }
    int nely() const { return nely_; }
    int nelz() const { return nelz_; }

    int nx() const { return nelx_ + 1; }
    int ny() const { return nely_ + 1; }
    int nz() const { return nelz_ + 1; }

    int nElems() const { return nelx_ * nely_ * nelz_; }
    int nNodes() const { return nx() * ny() * nz(); }
    int nDof() const { return 3 * nNodes(); }

    int nodeId(int i, int j, int k) const {
        return i + j * nx() + k * nx() * ny();
    }
    int elemId(int ex, int ey, int ez) const {
        return ex + ey * nelx_ + ez * nelx_ * nely_;
    }

    // 8 corner node ids of an element, local order l = di + 2*dj + 4*dk.
    std::array<int, 8> elementNodes(int ex, int ey, int ez) const {
        std::array<int, 8> n{};
        for (int dk = 0; dk < 2; ++dk)
            for (int dj = 0; dj < 2; ++dj)
                for (int di = 0; di < 2; ++di)
                    n[static_cast<size_t>(di + 2 * dj + 4 * dk)] =
                        nodeId(ex + di, ey + dj, ez + dk);
        return n;
    }

    // 24 DOF indices of an element (3 per corner node, x/y/z).
    std::array<int, 24> elementDofs(int ex, int ey, int ez) const {
        const auto nodes = elementNodes(ex, ey, ez);
        std::array<int, 24> dofs{};
        for (int a = 0; a < 8; ++a) {
            dofs[static_cast<size_t>(3 * a + 0)] = 3 * nodes[static_cast<size_t>(a)] + 0;
            dofs[static_cast<size_t>(3 * a + 1)] = 3 * nodes[static_cast<size_t>(a)] + 1;
            dofs[static_cast<size_t>(3 * a + 2)] = 3 * nodes[static_cast<size_t>(a)] + 2;
        }
        return dofs;
    }

private:
    int nelx_;
    int nely_;
    int nelz_;
};

} // namespace topopt
