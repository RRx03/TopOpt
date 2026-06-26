#pragma once

#include <functional>

#include <Eigen/Core>

namespace topopt {

// SIMP material interpolation, compliance sensitivities, and the
// Optimality-Criteria (OC) density update with bisection on the Lagrange
// multiplier. The density filter is injected as a callable so this module
// stays decoupled from the Helmholtz filter implementation.
class SIMP {
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

    explicit SIMP(const Params& p) : p_(p) {}

    // Per-element Young's modulus: E = Emin + rhoPhys^p (E0 - Emin).
    Vec youngModulus(const Vec& rhoPhys) const;

    // dc/d(rhoPhys)_e = -p * rhoPhys^(p-1) * (E0-Emin) * ce,
    // with ce = u_e^T KE0 u_e (unit-modulus element strain energy).
    Vec complianceSensitivity(const Vec& rhoPhys, const Vec& ce) const;

    struct OCResult {
        Vec rho;      // updated design field
        Vec rhoPhys;  // filtered (physical) field
    };

    // OC update. dc, dv are the *filtered* sensitivities. The volume
    // constraint is enforced on the filtered field via `filter`.
    OCResult ocUpdate(const Vec& rho, const Vec& dc, const Vec& dv,
                      const Filter& filter) const;

    const Params& params() const { return p_; }

private:
    Params p_;
};

} // namespace topopt
