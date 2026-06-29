#pragma once

#include <array>

namespace topopt {

// Structured 2D axisymmetric mesh of Q4 elements in the (r,z) half-plane.
// Domain [a,b] x [0,H], a = r_inner > 0 (axis r=0 is NOT meshed: see note).
//
// Node (i,j), 0-based, i along r (0..nr), j along z (0..nz):
//     node_id = i + j*(nr+1)
//     r(i) = a + i*hr,  hr = (b-a)/nr        z(j) = j*hz,  hz = H/nz
// DOF per node n: u_r -> 2n, u_z -> 2n+1.
//
// Element (ei,ej), local corner l = di + 2*dj (di,dj in {0,1}):
//     global node = node_id(ei+di, ej+dj)
// natural coords of local node l: (xi,eta) = (2*di-1, 2*dj-1).
// The 8 element DOFs are [n0_r,n0_z, n1_r,n1_z, n2_r,n2_z, n3_r,n3_z].
//
// Singularity note: the hoop strain eps_theta = u_r/r is finite here because
// a > 0. A geometry touching the axis (r=0) would require nodes at r=eps>0 or
// a dedicated near-axis formulation; not handled (and not needed for Lame).
class Grid2DAxi {
public:
    Grid2DAxi(int nr, int nz, double a, double b, double H)
        : nr_(nr), nz_(nz), a_(a), b_(b), H_(H),
          hr_((b - a) / nr), hz_(H / nz) {}

    int nr() const { return nr_; }
    int nz() const { return nz_; }

    int nrn() const { return nr_ + 1; }
    int nzn() const { return nz_ + 1; }

    double a() const { return a_; }
    double b() const { return b_; }
    double height() const { return H_; }
    double hr() const { return hr_; }
    double hz() const { return hz_; }

    double r(int i) const { return a_ + i * hr_; }
    double z(int j) const { return j * hz_; }

    int nElems() const { return nr_ * nz_; }
    int nNodes() const { return nrn() * nzn(); }
    int nDof() const { return 2 * nNodes(); }

    int nodeId(int i, int j) const { return i + j * nrn(); }
    int elemId(int ei, int ej) const { return ei + ej * nr_; }

    // 4 corner node ids of an element, local order l = di + 2*dj.
    std::array<int, 4> elementNodes(int ei, int ej) const {
        std::array<int, 4> n{};
        for (int dj = 0; dj < 2; ++dj)
            for (int di = 0; di < 2; ++di)
                n[static_cast<size_t>(di + 2 * dj)] = nodeId(ei + di, ej + dj);
        return n;
    }

    // 8 DOF indices of an element (2 per corner node, u_r/u_z).
    std::array<int, 8> elementDofs(int ei, int ej) const {
        const auto nodes = elementNodes(ei, ej);
        std::array<int, 8> dofs{};
        for (int a = 0; a < 4; ++a) {
            dofs[static_cast<size_t>(2 * a + 0)] =
                2 * nodes[static_cast<size_t>(a)] + 0;
            dofs[static_cast<size_t>(2 * a + 1)] =
                2 * nodes[static_cast<size_t>(a)] + 1;
        }
        return dofs;
    }

private:
    int nr_;
    int nz_;
    double a_;
    double b_;
    double H_;
    double hr_;
    double hz_;
};

} // namespace topopt
