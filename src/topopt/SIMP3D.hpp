#pragma once

#include <functional>

#include <Eigen/Core>

namespace topopt {

// SIMP material interpolation, compliance sensitivity, and the Optimality-
// Criteria density update (bisection on the Lagrange multiplier). Identical
// algorithm to Phase 1; dimension-agnostic (operates on per-element vectors).
// The density filter is injected as a callable, decoupling it from Helmholtz.
class SIMP3D {
public:
    using Vec = Eigen::VectorXd;
    using Filter = std::function<Vec(const Vec&)>;

    struct Params {
        double E0 = 1.0;
        double Emin = 1e-9;
        double penal = 3.0;
        double volfrac = 0.5;
        double move = 0.2;
    };

    explicit SIMP3D(const Params& p) : p_(p) {}

    // E = Emin + rhoPhys^p (E0 - Emin).
    Vec youngModulus(const Vec& rhoPhys) const;

    // dc/d(rhoPhys) = -p (E0-Emin) rhoPhys^(p-1) ce, ce = u_e^T KE0 u_e.
    Vec complianceSensitivity(const Vec& rhoPhys, const Vec& ce) const;

    struct OCResult {
        Vec rho;
        Vec rhoPhys;
    };

    // dc, dv are the filtered sensitivities. Volume enforced on the filtered field.
    OCResult ocUpdate(const Vec& rho, const Vec& dc, const Vec& dv,
                      const Filter& filter) const;

    const Params& params() const { return p_; }

private:
    Params p_;
};

} // namespace topopt
