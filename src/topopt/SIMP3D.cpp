#include "topopt/SIMP3D.hpp"

#include <algorithm>
#include <cmath>

namespace topopt {

SIMP3D::Vec SIMP3D::youngModulus(const Vec& rhoPhys) const {
    // Clamp to [0,1]: the Helmholtz filter (a PDE smoother) can slightly
    // undershoot below 0; pow(negative, non-integer p) -> NaN (cf. LL-008).
    const Vec r = rhoPhys.array().max(0.0).min(1.0);
    return p_.Emin + r.array().pow(p_.penal) * (p_.E0 - p_.Emin);
}

SIMP3D::Vec SIMP3D::complianceSensitivity(const Vec& rhoPhys, const Vec& ce) const {
    const Vec r = rhoPhys.array().max(0.0).min(1.0);  // see LL-008
    return -p_.penal * (p_.E0 - p_.Emin)
           * r.array().pow(p_.penal - 1.0) * ce.array();
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
    // Cap the bisection: defends against a non-shrinking bracket if rhoNew.sum()
    // ever becomes NaN (would otherwise spin forever with l1 stuck at 0).
    for (int iter = 0; iter < 100 && (l2 - l1) / (l1 + l2) > 1e-3; ++iter) {
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
