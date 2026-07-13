#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <Eigen/Core>

#include "core/Grid2DAxi.hpp"
#include "fem/AxiQ4Element.hpp"

namespace topopt {

// Axisymmetric (4-component) stress measures for stress-constrained TO.
// Stress order [sigma_r, sigma_z, sigma_theta, tau_rz] at element centroids.
//  - Per-element solid von Mises: vm0_e = sqrt( s_e^T Vax s_e ), with
//    s_e = S0ax_e u_e, S0ax_e = D*B at the element centroid (E=1), and the
//    axisymmetric von Mises quadratic form
//      Vax = [[ 1, -.5, -.5, 0],
//             [-.5,  1, -.5, 0],
//             [-.5, -.5,  1, 0],
//             [ 0,   0,   0, 3]].
//    NOTE: in axisymmetry the strain-displacement matrix B (and hence S0ax)
//    depends on r, so S0ax is PER ELEMENT, not a shared constant as in 3D.
//  - qp / eps-relaxation: constrained element stress sigma_e = rho_e^q vm0_e,
//    q < p_SIMP (removes the spurious singularity as rho->0, LL-LIT-001).
//  - p-norm aggregation: sigma_PN = ( sum_e sigma_e^P )^(1/P) ~ max_e sigma_e.
class StressModelAxi {
public:
    using Vec = Eigen::VectorXd;
    using Mat4 = AxiQ4Element::Mat4;
    using Vec4 = AxiQ4Element::Vec4;
    using Vec8 = AxiQ4Element::Vec8;

    StressModelAxi(double nu, double qRelax, double Pagg)
        : nu_(nu), q_(qRelax), P_(Pagg), V_(vonMisesForm()) {}

    // Axisymmetric von Mises quadratic form Vax (symmetric).
    static Mat4 vonMisesForm() {
        Mat4 V = Mat4::Zero();
        V(0, 0) = V(1, 1) = V(2, 2) = 1.0;
        V(0, 1) = V(1, 0) = -0.5;
        V(0, 2) = V(2, 0) = -0.5;
        V(1, 2) = V(2, 1) = -0.5;
        V(3, 3) = 3.0;
        return V;
    }

    // Solid (unrelaxed) von Mises per element (E=1), from global displacement U.
    Vec vonMisesSolid(const Grid2DAxi& grid, const Vec& U) const {
        Vec vm(grid.nElems());
        for (int ej = 0; ej < grid.nz(); ++ej)
            for (int ei = 0; ei < grid.nr(); ++ei) {
                std::array<double, 4> rn, zn;
                cornerCoords(grid, ei, ej, rn, zn);
                const auto S = AxiQ4Element::stressMatrix(rn, zn, nu_);
                const auto dofs = grid.elementDofs(ei, ej);
                Vec8 ue;
                for (int a = 0; a < 8; ++a)
                    ue(a) = U(dofs[static_cast<size_t>(a)]);
                const Vec4 s = S * ue;
                const double vm2 = s.transpose() * V_ * s;
                vm(grid.elemId(ei, ej)) = std::sqrt(std::max(vm2, 0.0));
            }
        return vm;
    }

    // Relaxed element stress sigma_e = rho_e^q vm0_e (clamp rho>=0, LL-008).
    Vec relaxedStress(const Vec& rho, const Vec& vm0) const {
        return rho.array().max(0.0).pow(q_) * vm0.array();
    }

    // p-norm aggregate of the relaxed stresses.
    double pNorm(const Vec& sigma) const {
        double s = 0.0;
        for (Eigen::Index e = 0; e < sigma.size(); ++e)
            s += std::pow(sigma(e), P_);
        return std::pow(s, 1.0 / P_);
    }

    // Corner coordinates of element (ei,ej) in local node order l = di + 2*dj.
    static void cornerCoords(const Grid2DAxi& grid, int ei, int ej,
                             std::array<double, 4>& rn,
                             std::array<double, 4>& zn) {
        for (int dj = 0; dj < 2; ++dj)
            for (int di = 0; di < 2; ++di) {
                const size_t l = static_cast<size_t>(di + 2 * dj);
                rn[l] = grid.r(ei + di);
                zn[l] = grid.z(ej + dj);
            }
    }

    double qRelax() const { return q_; }
    double Pagg() const { return P_; }
    double nu() const { return nu_; }
    const Mat4& V() const { return V_; }

private:
    double nu_;
    double q_;
    double P_;
    Mat4 V_;
};

} // namespace topopt
