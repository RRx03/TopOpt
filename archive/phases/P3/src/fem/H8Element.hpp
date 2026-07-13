#pragma once

#include <Eigen/Core>

namespace topopt {

// Trilinear 8-node hexahedron on the unit cube [0,1]^3.
// Provides the unit-modulus elastic stiffness KE0 (24x24, E=1) and the
// scalar Helmholtz element matrices Le (diffusion) and Me (mass), both 8x8,
// computed by 2x2x2 Gauss quadrature. Local node order: l = di + 2*dj + 4*dk.
class H8Element {
public:
    using Mat24 = Eigen::Matrix<double, 24, 24>;
    using Mat8 = Eigen::Matrix<double, 8, 8>;

    // Elastic stiffness for Young's modulus E=1, Poisson ratio nu (isotropic).
    static Mat24 stiffness(double nu);

    // Helmholtz diffusion (∫ ∇N·∇N) and mass (∫ N N) matrices on the unit cube.
    static Mat8 diffusion();
    static Mat8 mass();
};

} // namespace topopt
