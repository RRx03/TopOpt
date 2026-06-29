#pragma once

#include <array>

#include <Eigen/Core>

namespace topopt {

// Bilinear 4-node quadrilateral, axisymmetric (r,z) formulation.
// Strain order [eps_r, eps_z, eps_theta, gamma_rz]:
//   eps_r = du_r/dr, eps_z = du_z/dz, eps_theta = u_r/r,
//   gamma_rz = du_r/dz + du_z/dr.
// Local node order l = di + 2*dj, natural coords (xi,eta) = (2*di-1, 2*dj-1).
// The 8 element DOFs are [u_r0,u_z0, u_r1,u_z1, u_r2,u_z2, u_r3,u_z3].
//
// Integration: K_e = sum_gauss w * B^T D B * (r_g * detJ), 2x2 Gauss, with
// r_g = sum_a N_a(g) r_a. The 2*pi from the circumferential integral is a
// constant common to K and to the consistent nodal loads, so it is omitted
// consistently (it cancels in K U = F).
class AxiQ4Element {
public:
    using Mat8 = Eigen::Matrix<double, 8, 8>;
    using Mat4x8 = Eigen::Matrix<double, 4, 8>;
    using Mat4 = Eigen::Matrix<double, 4, 4>;
    using Vec4 = Eigen::Matrix<double, 4, 1>;
    using Vec8 = Eigen::Matrix<double, 8, 1>;

    // Element stiffness (8x8) for Young's modulus E=1, Poisson ratio nu.
    // r_nodes/z_nodes are the corner coordinates in local node order.
    static Mat8 stiffness(const std::array<double, 4>& r_nodes,
                          const std::array<double, 4>& z_nodes, double nu);

    // Stress matrix S = D*B (4x8) evaluated at the element centroid, E=1.
    // sigma = S * u_e gives [sigma_r, sigma_z, sigma_theta, tau_rz] at centre.
    static Mat4x8 stressMatrix(const std::array<double, 4>& r_nodes,
                               const std::array<double, 4>& z_nodes, double nu);

    // Isotropic axisymmetric elasticity matrix D (4x4) for E=1.
    static Mat4 elasticD(double nu);
};

} // namespace topopt
