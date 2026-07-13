#include "fem/AxiQ4Element.hpp"

#include <cmath>

#include <Eigen/LU>

namespace topopt {

namespace {

// Local node natural coordinates, order l = di + 2*dj, in {-1,+1}.
struct NodeNat {
    double xi, eta;
};
std::array<NodeNat, 4> nodeNat() {
    std::array<NodeNat, 4> nn{};
    for (int dj = 0; dj < 2; ++dj)
        for (int di = 0; di < 2; ++di) {
            const int l = di + 2 * dj;
            nn[static_cast<size_t>(l)] = {2.0 * di - 1.0, 2.0 * dj - 1.0};
        }
    return nn;
}

void shapeVal(double xi, double eta, const std::array<NodeNat, 4>& nn,
              double N[4]) {
    for (int a = 0; a < 4; ++a) {
        const NodeNat& p = nn[static_cast<size_t>(a)];
        N[a] = 0.25 * (1 + p.xi * xi) * (1 + p.eta * eta);
    }
}

// Shape-function derivatives w.r.t. natural coords: dN[a][0]=d/dxi, [1]=d/deta.
void shapeDeriv(double xi, double eta, const std::array<NodeNat, 4>& nn,
                double dN[4][2]) {
    for (int a = 0; a < 4; ++a) {
        const NodeNat& p = nn[static_cast<size_t>(a)];
        dN[a][0] = 0.25 * p.xi * (1 + p.eta * eta);
        dN[a][1] = 0.25 * (1 + p.xi * xi) * p.eta;
    }
}

// Build B (4x8), r_g and detJ at natural point (xi,eta).
// Coordinates: axis 0 = r, axis 1 = z.
void evalB(double xi, double eta, const std::array<NodeNat, 4>& nn,
           const std::array<double, 4>& r_nodes,
           const std::array<double, 4>& z_nodes,
           AxiQ4Element::Mat4x8& B, double& r_g, double& detJ) {
    double N[4];
    double dN[4][2];
    shapeVal(xi, eta, nn, N);
    shapeDeriv(xi, eta, nn, dN);

    // Jacobian J(d_nat, d_phys): rows = (xi,eta), cols = (r,z).
    Eigen::Matrix2d J = Eigen::Matrix2d::Zero();
    r_g = 0.0;
    for (int a = 0; a < 4; ++a) {
        const double ra = r_nodes[static_cast<size_t>(a)];
        const double za = z_nodes[static_cast<size_t>(a)];
        J(0, 0) += dN[a][0] * ra;
        J(0, 1) += dN[a][0] * za;
        J(1, 0) += dN[a][1] * ra;
        J(1, 1) += dN[a][1] * za;
        r_g += N[a] * ra;
    }
    detJ = J.determinant();
    const Eigen::Matrix2d Jinv = J.inverse();

    // Physical derivatives: dNdr = Jinv(0,0)*dN_dxi + Jinv(0,1)*dN_deta, etc.
    B.setZero();
    for (int a = 0; a < 4; ++a) {
        const double dNdr = Jinv(0, 0) * dN[a][0] + Jinv(0, 1) * dN[a][1];
        const double dNdz = Jinv(1, 0) * dN[a][0] + Jinv(1, 1) * dN[a][1];
        const int cr = 2 * a, cz = 2 * a + 1;
        B(0, cr) = dNdr;                          // eps_r   = du_r/dr
        B(1, cz) = dNdz;                          // eps_z   = du_z/dz
        B(2, cr) = N[a] / r_g;                    // eps_th  = u_r/r
        B(3, cr) = dNdz; B(3, cz) = dNdr;         // gamma_rz
    }
}

} // namespace

AxiQ4Element::Mat4 AxiQ4Element::elasticD(double nu) {
    Mat4 D = Mat4::Zero();
    const double f = 1.0 / ((1.0 + nu) * (1.0 - 2.0 * nu));
    const double a = f * (1.0 - nu);
    const double b = f * nu;
    const double c = f * (1.0 - 2.0 * nu) / 2.0;
    // Order [r, z, theta, rz].
    D(0, 0) = D(1, 1) = D(2, 2) = a;
    D(0, 1) = D(1, 0) = b;
    D(0, 2) = D(2, 0) = b;
    D(1, 2) = D(2, 1) = b;
    D(3, 3) = c;
    return D;
}

AxiQ4Element::Mat8 AxiQ4Element::stiffness(const std::array<double, 4>& r_nodes,
                                          const std::array<double, 4>& z_nodes,
                                          double nu) {
    const auto nn = nodeNat();
    const Mat4 D = elasticD(nu);
    const double g = 1.0 / std::sqrt(3.0);
    const double gp[2] = {-g, g};

    Mat8 KE = Mat8::Zero();
    for (int ig = 0; ig < 2; ++ig)
        for (int jg = 0; jg < 2; ++jg) {
            Mat4x8 B;
            double r_g, detJ;
            evalB(gp[ig], gp[jg], nn, r_nodes, z_nodes, B, r_g, detJ);
            KE += (B.transpose() * D * B) * (r_g * detJ);  // Gauss weight = 1
        }
    return KE;
}

AxiQ4Element::Mat4x8 AxiQ4Element::stressMatrix(
    const std::array<double, 4>& r_nodes, const std::array<double, 4>& z_nodes,
    double nu) {
    const auto nn = nodeNat();
    const Mat4 D = elasticD(nu);
    Mat4x8 B;
    double r_g, detJ;
    evalB(0.0, 0.0, nn, r_nodes, z_nodes, B, r_g, detJ);  // centroid
    return D * B;
}

} // namespace topopt
