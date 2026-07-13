#pragma once

#include <algorithm>
#include <cmath>

#include <Eigen/Core>

#include "core/Grid3D.hpp"
#include "fem/H8Element.hpp"

namespace topopt {

// Stress measures for stress-constrained TO.
//  - Per-element solid von Mises: vm0_e = sqrt( (S0 u_e)^T V (S0 u_e) ),
//    S0 = D0*B at centroid (E=1), V the von Mises quadratic form.
//  - qp / eps-relaxation (Bruggi 2008, Duysinx-Bendsoe 1998): the constrained
//    element stress is sigma_e = rho_e^q * vm0_e, with q < p_SIMP. As rho->0 the
//    relaxed stress vanishes, removing the spurious singularity that otherwise
//    makes the optimiser refuse to create void (cf. LL-LIT-001).
//  - p-norm aggregation: sigma_PN = ( sum_e sigma_e^P )^(1/P) ~ max_e sigma_e,
//    P the aggregation exponent (8-12), distinct from the SIMP exponent.
class StressModel {
public:
    using Vec = Eigen::VectorXd;

    StressModel(double nu, double qRelax, double Pagg)
        : S0_(H8Element::stressMatrix(nu)), V_(H8Element::vonMisesForm()),
          q_(qRelax), P_(Pagg) {}

    // Solid (unrelaxed) von Mises per element, from global displacement U.
    Vec vonMisesSolid(const Grid3D& grid, const Vec& U) const {
        Vec vm(grid.nElems());
        for (int ez = 0; ez < grid.nelz(); ++ez)
            for (int ey = 0; ey < grid.nely(); ++ey)
                for (int ex = 0; ex < grid.nelx(); ++ex) {
                    const auto dofs = grid.elementDofs(ex, ey, ez);
                    Eigen::Matrix<double, 24, 1> ue;
                    for (int a = 0; a < 24; ++a) ue(a) = U(dofs[static_cast<size_t>(a)]);
                    const Eigen::Matrix<double, 6, 1> s = S0_ * ue;
                    const double vm2 = s.transpose() * V_ * s;
                    vm(grid.elemId(ex, ey, ez)) = std::sqrt(std::max(vm2, 0.0));
                }
        return vm;
    }

    // Relaxed element stress sigma_e = rho_e^q * vm0_e.
    Vec relaxedStress(const Vec& rho, const Vec& vm0) const {
        return rho.array().max(0.0).pow(q_) * vm0.array();  // clamp rho>=0 (LL-008)
    }

    // p-norm aggregate of the relaxed stresses.
    double pNorm(const Vec& sigma) const {
        double s = 0.0;
        for (Eigen::Index e = 0; e < sigma.size(); ++e)
            s += std::pow(sigma(e), P_);
        return std::pow(s, 1.0 / P_);
    }

    double qRelax() const { return q_; }
    double Pagg() const { return P_; }
    const H8Element::Mat6x24& S0() const { return S0_; }
    const H8Element::Mat6& V() const { return V_; }

private:
    H8Element::Mat6x24 S0_;
    H8Element::Mat6 V_;
    double q_;
    double P_;
};

} // namespace topopt
