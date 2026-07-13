#include "fem/H8Element.hpp"

#include <array>
#include <cmath>

#include <Eigen/LU>

namespace topopt {

namespace {

// Local node natural coordinates, order l = di + 2*dj + 4*dk, in {-1,+1}.
struct NodeNat {
    double xi, eta, zeta;
};
std::array<NodeNat, 8> nodeNat() {
    std::array<NodeNat, 8> nn{};
    for (int dk = 0; dk < 2; ++dk)
        for (int dj = 0; dj < 2; ++dj)
            for (int di = 0; di < 2; ++di) {
                const int l = di + 2 * dj + 4 * dk;
                nn[static_cast<size_t>(l)] = {2.0 * di - 1.0, 2.0 * dj - 1.0,
                                              2.0 * dk - 1.0};
            }
    return nn;
}

// Shape-function derivatives w.r.t. natural coords at (xi,eta,zeta).
// Returns dN[a][d], d = 0,1,2 for d/dxi, d/deta, d/dzeta.
void shapeDeriv(double xi, double eta, double zeta,
                const std::array<NodeNat, 8>& nn,
                double dN[8][3]) {
    for (int a = 0; a < 8; ++a) {
        const NodeNat& p = nn[static_cast<size_t>(a)];
        dN[a][0] = 0.125 * p.xi * (1 + p.eta * eta) * (1 + p.zeta * zeta);
        dN[a][1] = 0.125 * (1 + p.xi * xi) * p.eta * (1 + p.zeta * zeta);
        dN[a][2] = 0.125 * (1 + p.xi * xi) * (1 + p.eta * eta) * p.zeta;
    }
}

void shapeVal(double xi, double eta, double zeta,
              const std::array<NodeNat, 8>& nn, double N[8]) {
    for (int a = 0; a < 8; ++a) {
        const NodeNat& p = nn[static_cast<size_t>(a)];
        N[a] = 0.125 * (1 + p.xi * xi) * (1 + p.eta * eta) * (1 + p.zeta * zeta);
    }
}

// Isotropic 3D elasticity matrix D (6x6) for E=1.
Eigen::Matrix<double, 6, 6> elasticD(double nu) {
    Eigen::Matrix<double, 6, 6> D = Eigen::Matrix<double, 6, 6>::Zero();
    const double f = 1.0 / ((1.0 + nu) * (1.0 - 2.0 * nu));
    const double a = f * (1.0 - nu);
    const double b = f * nu;
    const double c = f * (1.0 - 2.0 * nu) / 2.0;
    D(0, 0) = D(1, 1) = D(2, 2) = a;
    D(0, 1) = D(1, 0) = D(0, 2) = D(2, 0) = D(1, 2) = D(2, 1) = b;
    D(3, 3) = D(4, 4) = D(5, 5) = c;
    return D;
}

} // namespace

H8Element::Mat24 H8Element::stiffness(double nu) {
    const auto nn = nodeNat();
    const Eigen::Matrix<double, 6, 6> D = elasticD(nu);
    // Unit-cube node coordinates X_a = (di,dj,dk) in [0,1].
    double X[8][3];
    for (int a = 0; a < 8; ++a) {
        X[a][0] = (nn[static_cast<size_t>(a)].xi + 1.0) * 0.5;
        X[a][1] = (nn[static_cast<size_t>(a)].eta + 1.0) * 0.5;
        X[a][2] = (nn[static_cast<size_t>(a)].zeta + 1.0) * 0.5;
    }

    const double g = 1.0 / std::sqrt(3.0);
    const double gp[2] = {-g, g};

    Mat24 KE = Mat24::Zero();
    for (int ig = 0; ig < 2; ++ig)
        for (int jg = 0; jg < 2; ++jg)
            for (int kg = 0; kg < 2; ++kg) {
                double dN[8][3];
                shapeDeriv(gp[ig], gp[jg], gp[kg], nn, dN);

                // Jacobian J = sum_a X_a (x) dN_a (3x3).
                Eigen::Matrix3d J = Eigen::Matrix3d::Zero();
                for (int a = 0; a < 8; ++a)
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c)
                            J(r, c) += X[a][r] * dN[a][c];

                const double detJ = J.determinant();
                const Eigen::Matrix3d Jinv = J.inverse();

                // Cartesian derivatives dNdx[a][i] = sum_c Jinv(c,i) dN[a][c].
                double dNdx[8][3];
                for (int a = 0; a < 8; ++a)
                    for (int i = 0; i < 3; ++i) {
                        double s = 0.0;
                        for (int c = 0; c < 3; ++c) s += Jinv(c, i) * dN[a][c];
                        dNdx[a][i] = s;
                    }

                // Strain-displacement B (6x24).
                Eigen::Matrix<double, 6, 24> B =
                    Eigen::Matrix<double, 6, 24>::Zero();
                for (int a = 0; a < 8; ++a) {
                    const double bx = dNdx[a][0], by = dNdx[a][1], bz = dNdx[a][2];
                    const int cx = 3 * a, cy = 3 * a + 1, cz = 3 * a + 2;
                    B(0, cx) = bx;
                    B(1, cy) = by;
                    B(2, cz) = bz;
                    B(3, cx) = by; B(3, cy) = bx;          // xy
                    B(4, cy) = bz; B(4, cz) = by;          // yz
                    B(5, cx) = bz; B(5, cz) = bx;          // xz
                }
                KE += (B.transpose() * D * B) * detJ;  // Gauss weight = 1
            }
    return KE;
}

H8Element::Mat8 H8Element::diffusion() {
    const auto nn = nodeNat();
    double X[8][3];
    for (int a = 0; a < 8; ++a) {
        X[a][0] = (nn[static_cast<size_t>(a)].xi + 1.0) * 0.5;
        X[a][1] = (nn[static_cast<size_t>(a)].eta + 1.0) * 0.5;
        X[a][2] = (nn[static_cast<size_t>(a)].zeta + 1.0) * 0.5;
    }
    const double g = 1.0 / std::sqrt(3.0);
    const double gp[2] = {-g, g};

    Mat8 Le = Mat8::Zero();
    for (int ig = 0; ig < 2; ++ig)
        for (int jg = 0; jg < 2; ++jg)
            for (int kg = 0; kg < 2; ++kg) {
                double dN[8][3];
                shapeDeriv(gp[ig], gp[jg], gp[kg], nn, dN);
                Eigen::Matrix3d J = Eigen::Matrix3d::Zero();
                for (int a = 0; a < 8; ++a)
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c)
                            J(r, c) += X[a][r] * dN[a][c];
                const double detJ = J.determinant();
                const Eigen::Matrix3d Jinv = J.inverse();
                Eigen::Matrix<double, 8, 3> G;
                for (int a = 0; a < 8; ++a)
                    for (int i = 0; i < 3; ++i) {
                        double s = 0.0;
                        for (int c = 0; c < 3; ++c) s += Jinv(c, i) * dN[a][c];
                        G(a, i) = s;
                    }
                Le += (G * G.transpose()) * detJ;
            }
    return Le;
}

H8Element::Mat8 H8Element::mass() {
    const auto nn = nodeNat();
    const double g = 1.0 / std::sqrt(3.0);
    const double gp[2] = {-g, g};
    // Unit cube: detJ = 1/8 constant.
    Mat8 Me = Mat8::Zero();
    for (int ig = 0; ig < 2; ++ig)
        for (int jg = 0; jg < 2; ++jg)
            for (int kg = 0; kg < 2; ++kg) {
                double N[8];
                shapeVal(gp[ig], gp[jg], gp[kg], nn, N);
                Eigen::Map<Eigen::Matrix<double, 8, 1>> Nv(N);
                Me += (Nv * Nv.transpose()) * (1.0 / 8.0);
            }
    return Me;
}

H8Element::Mat24x8 H8Element::thermalCoupling(double nu) {
    const auto nn = nodeNat();
    const Eigen::Matrix<double, 6, 6> D = elasticD(nu);
    Eigen::Matrix<double, 6, 1> m;
    m << 1, 1, 1, 0, 0, 0;  // isotropic expansion (Voigt)
    const Eigen::Matrix<double, 6, 1> Dm = D * m;

    double X[8][3];
    for (int a = 0; a < 8; ++a) {
        X[a][0] = (nn[static_cast<size_t>(a)].xi + 1.0) * 0.5;
        X[a][1] = (nn[static_cast<size_t>(a)].eta + 1.0) * 0.5;
        X[a][2] = (nn[static_cast<size_t>(a)].zeta + 1.0) * 0.5;
    }
    const double g = 1.0 / std::sqrt(3.0);
    const double gp[2] = {-g, g};

    Mat24x8 Cth = Mat24x8::Zero();
    for (int ig = 0; ig < 2; ++ig)
        for (int jg = 0; jg < 2; ++jg)
            for (int kg = 0; kg < 2; ++kg) {
                double dN[8][3], N[8];
                shapeDeriv(gp[ig], gp[jg], gp[kg], nn, dN);
                shapeVal(gp[ig], gp[jg], gp[kg], nn, N);

                Eigen::Matrix3d J = Eigen::Matrix3d::Zero();
                for (int a = 0; a < 8; ++a)
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c)
                            J(r, c) += X[a][r] * dN[a][c];
                const double detJ = J.determinant();
                const Eigen::Matrix3d Jinv = J.inverse();

                double dNdx[8][3];
                for (int a = 0; a < 8; ++a)
                    for (int i = 0; i < 3; ++i) {
                        double s = 0.0;
                        for (int c = 0; c < 3; ++c) s += Jinv(c, i) * dN[a][c];
                        dNdx[a][i] = s;
                    }

                Eigen::Matrix<double, 6, 24> B = Eigen::Matrix<double, 6, 24>::Zero();
                for (int a = 0; a < 8; ++a) {
                    const double bx = dNdx[a][0], by = dNdx[a][1], bz = dNdx[a][2];
                    const int cx = 3 * a, cy = 3 * a + 1, cz = 3 * a + 2;
                    B(0, cx) = bx; B(1, cy) = by; B(2, cz) = bz;
                    B(3, cx) = by; B(3, cy) = bx;
                    B(4, cy) = bz; B(4, cz) = by;
                    B(5, cx) = bz; B(5, cz) = bx;
                }
                // (B^T D m) is 24x1 ; outer with N (1x8) -> 24x8.
                const Eigen::Matrix<double, 24, 1> BtDm = B.transpose() * Dm;
                Eigen::Map<const Eigen::Matrix<double, 8, 1>> Nv(N);
                Cth += (BtDm * Nv.transpose()) * detJ;
            }
    return Cth;
}

H8Element::Mat6x24 H8Element::stressMatrix(double nu) {
    const auto nn = nodeNat();
    const Eigen::Matrix<double, 6, 6> D = elasticD(nu);
    double X[8][3];
    for (int a = 0; a < 8; ++a) {
        X[a][0] = (nn[static_cast<size_t>(a)].xi + 1.0) * 0.5;
        X[a][1] = (nn[static_cast<size_t>(a)].eta + 1.0) * 0.5;
        X[a][2] = (nn[static_cast<size_t>(a)].zeta + 1.0) * 0.5;
    }
    // B at the centroid (xi=eta=zeta=0).
    double dN[8][3];
    shapeDeriv(0.0, 0.0, 0.0, nn, dN);
    Eigen::Matrix3d J = Eigen::Matrix3d::Zero();
    for (int a = 0; a < 8; ++a)
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) J(r, c) += X[a][r] * dN[a][c];
    const Eigen::Matrix3d Jinv = J.inverse();
    double dNdx[8][3];
    for (int a = 0; a < 8; ++a)
        for (int i = 0; i < 3; ++i) {
            double s = 0.0;
            for (int c = 0; c < 3; ++c) s += Jinv(c, i) * dN[a][c];
            dNdx[a][i] = s;
        }
    Mat6x24 B = Mat6x24::Zero();
    for (int a = 0; a < 8; ++a) {
        const double bx = dNdx[a][0], by = dNdx[a][1], bz = dNdx[a][2];
        const int cx = 3 * a, cy = 3 * a + 1, cz = 3 * a + 2;
        B(0, cx) = bx; B(1, cy) = by; B(2, cz) = bz;
        B(3, cx) = by; B(3, cy) = bx;
        B(4, cy) = bz; B(4, cz) = by;
        B(5, cx) = bz; B(5, cz) = bx;
    }
    return D * B;
}

H8Element::Mat6 H8Element::vonMisesForm() {
    Mat6 V = Mat6::Zero();
    V(0, 0) = V(1, 1) = V(2, 2) = 1.0;
    V(0, 1) = V(1, 0) = V(0, 2) = V(2, 0) = V(1, 2) = V(2, 1) = -0.5;
    V(3, 3) = V(4, 4) = V(5, 5) = 3.0;
    return V;
}

} // namespace topopt
