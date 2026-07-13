#pragma once

#include <Eigen/Core>

#include "core/Grid3D.hpp"
#include "fem/H8Element.hpp"

namespace topopt {

// Weak (one-way) thermo-elastic coupling: a temperature field induces a thermal
// strain eps_th = alpha (T - T_ref) m, hence an equivalent elastic load
//   f_thermal_e = E_e * alpha * Cth * (T_nodal_e - T_ref)
// assembled into a global force vector. The mechanical solve then uses
//   K(E) U = F_mech + F_thermal.
// One-way: T drives U, not the reverse.
class ThermoElasticCoupling {
public:
    using Vec = Eigen::VectorXd;

    explicit ThermoElasticCoupling(double nu)
        : cth_(H8Element::thermalCoupling(nu)) {}

    // Global thermal load (nDof). Emod: per-element Young modulus (nElems);
    // T: nodal temperatures (nNodes); Tref, alpha scalars.
    Vec thermalLoad(const Grid3D& grid, const Vec& Emod, const Vec& T,
                    double Tref, double alpha) const {
        Vec F = Vec::Zero(grid.nDof());
        for (int ez = 0; ez < grid.nelz(); ++ez)
            for (int ey = 0; ey < grid.nely(); ++ey)
                for (int ex = 0; ex < grid.nelx(); ++ex) {
                    const auto nodes = grid.elementNodes(ex, ey, ez);
                    const auto dofs = grid.elementDofs(ex, ey, ez);
                    Eigen::Matrix<double, 8, 1> dT;
                    for (int a = 0; a < 8; ++a)
                        dT(a) = T(nodes[static_cast<size_t>(a)]) - Tref;
                    const double scale =
                        Emod(grid.elemId(ex, ey, ez)) * alpha;
                    const Eigen::Matrix<double, 24, 1> fe = scale * (cth_ * dT);
                    for (int a = 0; a < 24; ++a)
                        F(dofs[static_cast<size_t>(a)]) += fe(a);
                }
        return F;
    }

    const H8Element::Mat24x8& couplingMatrix() const { return cth_; }

private:
    H8Element::Mat24x8 cth_;
};

} // namespace topopt
