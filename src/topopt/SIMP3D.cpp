#include "topopt/SIMP3D.hpp"

#include <algorithm>
#include <cmath>

namespace topopt {

SIMP3D::Vec SIMP3D::youngModulus(const Vec& rhoPhys) const {
    return p_.Emin + rhoPhys.array().pow(p_.penal) * (p_.E0 - p_.Emin);
}

SIMP3D::Vec SIMP3D::complianceSensitivity(const Vec& rhoPhys, const Vec& ce) const {
    return -p_.penal * (p_.E0 - p_.Emin)
           * rhoPhys.array().pow(p_.penal - 1.0) * ce.array();
}

SIMP3D::OCResult SIMP3D::ocUpdate(const Vec& rho, const Vec& dc, const Vec& dv,
                                  const Filter& filter) const {
    const Eigen::Index n = rho.size();
    const double target = p_.volfrac * static_cast<double>(n);

    const Vec Be = (-dc.array() / dv.array()).matrix();

    // The Helmholtz filter conserves the mean (sum), so the volume of the
    // filtered field equals that of the design field. We therefore drive the
    // bisection on rhoNew.sum() and apply the (expensive, iterative) filter
    // only once at the end -- critical for large 3D problems.
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
        if (rhoNew.sum() > target)
            l1 = lmid;
        else
            l2 = lmid;
    }
    return {rhoNew, filter(rhoNew)};
}

} // namespace topopt
