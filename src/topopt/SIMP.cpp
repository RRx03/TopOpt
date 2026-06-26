#include "topopt/SIMP.hpp"

#include <algorithm>
#include <cmath>

namespace topopt {

SIMP::Vec SIMP::youngModulus(const Vec& rhoPhys) const {
    return p_.Emin + rhoPhys.array().pow(p_.penal) * (p_.E0 - p_.Emin);
}

SIMP::Vec SIMP::complianceSensitivity(const Vec& rhoPhys, const Vec& ce) const {
    return -p_.penal * (p_.E0 - p_.Emin)
           * rhoPhys.array().pow(p_.penal - 1.0) * ce.array();
}

SIMP::OCResult SIMP::ocUpdate(const Vec& rho, const Vec& dc, const Vec& dv,
                              const Filter& filter) const {
    const Eigen::Index n = rho.size();
    const double target = p_.volfrac * static_cast<double>(n);

    // Be = -dc / dv  (positive for a well-posed minimization).
    const Vec Be = (-dc.array() / dv.array()).matrix();

    double l1 = 0.0, l2 = 1e9;
    Vec rhoNew = rho;
    while ((l2 - l1) / (l1 + l2) > 1e-3) {
        const double lmid = 0.5 * (l1 + l2);
        for (Eigen::Index e = 0; e < n; ++e) {
            const double cand = rho(e) * std::sqrt(Be(e) / lmid);
            double v = std::min(rho(e) + p_.move, cand);
            v = std::min(1.0, v);
            v = std::max(rho(e) - p_.move, v);
            v = std::max(0.0, v);
            rhoNew(e) = v;
        }
        const Vec rhoPhys = filter(rhoNew);
        if (rhoPhys.sum() > target)
            l1 = lmid;
        else
            l2 = lmid;
    }
    return {rhoNew, filter(rhoNew)};
}

} // namespace topopt
