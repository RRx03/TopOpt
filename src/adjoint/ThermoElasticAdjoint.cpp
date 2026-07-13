#include "adjoint/ThermoElasticAdjoint.hpp"

#include <algorithm>
#include <cmath>

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>

namespace topopt {

namespace {
inline double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
}  // namespace

ThermoElasticAdjoint::ThermoElasticAdjoint(
    const Grid3D& grid, const Material& mat,
    const std::vector<int>& elasticFixedDofs,
    const std::vector<int>& thermalFixedNodes, const Vec& Fmech, const Vec& Q)
    : grid_(grid), mat_(mat), Fmech_(Fmech), Q_(Q), fem_(grid, mat.nu) {
    fem_.setFixedDofs(elasticFixedDofs);
    ke0_ = fem_.KE0();
    l0_ = H8Element::diffusion();
    cth_ = H8Element::thermalCoupling(mat.nu);

    const int nNodes = grid_.nNodes();
    std::vector<char> fixed(static_cast<size_t>(nNodes), 0);
    for (int n : thermalFixedNodes) fixed[static_cast<size_t>(n)] = 1;
    thermalDofMap_.assign(static_cast<size_t>(nNodes), -1);
    nFreeT_ = 0;
    for (int n = 0; n < nNodes; ++n)
        if (!fixed[static_cast<size_t>(n)])
            thermalDofMap_[static_cast<size_t>(n)] = nFreeT_++;
}

ThermoElasticAdjoint::Vec ThermoElasticAdjoint::youngModulus(
    const Vec& rho) const {
    Vec E(rho.size());
    for (int e = 0; e < rho.size(); ++e)
        E(e) = mat_.Emin +
               std::pow(clamp01(rho(e)), mat_.p) * (mat_.E0 - mat_.Emin);
    return E;
}

ThermoElasticAdjoint::Vec ThermoElasticAdjoint::conductivity(
    const Vec& rho) const {
    Vec k(rho.size());
    for (int e = 0; e < rho.size(); ++e)
        k(e) = mat_.kmin +
               std::pow(clamp01(rho(e)), mat_.q) * (mat_.k0 - mat_.kmin);
    return k;
}

// F_th = sum_e E_e alpha C_e (T_e - Tref), assembled into the global DOF vector.
ThermoElasticAdjoint::Vec ThermoElasticAdjoint::thermalLoad(const Vec& Evec,
                                                            const Vec& T) const {
    Vec F = Vec::Zero(grid_.nDof());
    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const auto dofs = grid_.elementDofs(ex, ey, ez);
                Eigen::Matrix<double, 8, 1> dT;
                for (int a = 0; a < 8; ++a)
                    dT(a) = T(nodes[static_cast<size_t>(a)]) - mat_.Tref;
                const double scale = Evec(grid_.elemId(ex, ey, ez)) * mat_.alpha;
                const Eigen::Matrix<double, 24, 1> fe = scale * (cth_ * dT);
                for (int a = 0; a < 24; ++a)
                    F(dofs[static_cast<size_t>(a)]) += fe(a);
            }
    return F;
}

// Assemble K_t = sum_e k_e L0 with Dirichlet nodes condensed; solve K_t x = rhs.
// Returns the full nodal vector (Dirichlet nodes set to 0).
ThermoElasticAdjoint::Vec ThermoElasticAdjoint::solveThermal(
    const Vec& kvec, const Vec& rhs) const {
    const int n = nFreeT_;
    std::vector<Eigen::Triplet<double>> trip;
    trip.reserve(static_cast<size_t>(grid_.nElems()) * 64);

    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const double ke = kvec(grid_.elemId(ex, ey, ez));
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                for (int a = 0; a < 8; ++a) {
                    const int ra =
                        thermalDofMap_[static_cast<size_t>(nodes[static_cast<size_t>(a)])];
                    if (ra < 0) continue;
                    for (int b = 0; b < 8; ++b) {
                        const int rb =
                            thermalDofMap_[static_cast<size_t>(nodes[static_cast<size_t>(b)])];
                        if (rb < 0) continue;
                        trip.emplace_back(ra, rb, ke * l0_(a, b));
                    }
                }
            }

    Eigen::SparseMatrix<double> K(n, n);
    K.setFromTriplets(trip.begin(), trip.end());
    K.makeCompressed();

    Vec r(n);
    for (int node = 0; node < grid_.nNodes(); ++node) {
        const int rd = thermalDofMap_[static_cast<size_t>(node)];
        if (rd >= 0) r(rd) = rhs(node);
    }

    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
    solver.compute(K);
    const Vec xr = solver.solve(r);

    Vec x = Vec::Zero(grid_.nNodes());
    for (int node = 0; node < grid_.nNodes(); ++node) {
        const int rd = thermalDofMap_[static_cast<size_t>(node)];
        if (rd >= 0) x(node) = xr(rd);
    }
    return x;
}

void ThermoElasticAdjoint::forward(const Vec& rho, Vec& T, Vec& U) const {
    const Vec E = youngModulus(rho);
    const Vec k = conductivity(rho);
    T = solveThermal(k, Q_);
    const Vec Fth = thermalLoad(E, T);
    U = fem_.solve(E, Fmech_ + Fth);
}

double ThermoElasticAdjoint::objective(const Vec& rho) const {
    Vec T, U;
    forward(rho, T, U);
    return Fmech_.dot(U);
}

ThermoElasticAdjoint::Solution ThermoElasticAdjoint::solve(
    const Vec& rho) const {
    Solution sol;
    forward(rho, sol.T, sol.U);
    sol.J = Fmech_.dot(sol.U);

    const Vec E = youngModulus(rho);
    const Vec k = conductivity(rho);

    // (1) Elastic adjoint: K_e lam_e = -L, with L = F_mech.
    const Vec lamE = fem_.solve(E, -Fmech_);

    // (2) Thermal adjoint: K_t lam_t = G^T lam_e.
    const Vec lamT = solveThermal(k, thermalAdjointRhs(E, lamE));

    // (3) Gradient = the three shared rho-derivative terms (no explicit term).
    hereditaryGradient(rho, sol.U, sol.T, lamE, lamT, sol.termElastic,
                       sol.termThermalLoad, sol.termConduction);
    sol.grad = sol.termElastic + sol.termThermalLoad + sol.termConduction;

    return sol;
}

// RHS of the thermal adjoint: g = G^T lam_e, G = dF_th/dT.
// Per element: 8-vector  E_e alpha C_e^T lam_e_local, assembled by node.
ThermoElasticAdjoint::Vec ThermoElasticAdjoint::thermalAdjointRhs(
    const Vec& Evec, const Vec& lamE) const {
    Vec gT = Vec::Zero(grid_.nNodes());
    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const auto dofs = grid_.elementDofs(ex, ey, ez);
                Eigen::Matrix<double, 24, 1> le;
                for (int a = 0; a < 24; ++a)
                    le(a) = lamE(dofs[static_cast<size_t>(a)]);
                const double scale = Evec(grid_.elemId(ex, ey, ez)) * mat_.alpha;
                const Eigen::Matrix<double, 8, 1> ce =
                    scale * (cth_.transpose() * le);
                for (int a = 0; a < 8; ++a)
                    gT(nodes[static_cast<size_t>(a)]) += ce(a);
            }
    return gT;
}

void ThermoElasticAdjoint::hereditaryGradient(
    const Vec& rho, const Vec& U, const Vec& T, const Vec& lamE,
    const Vec& lamT, Vec& termElastic, Vec& termThermalLoad,
    Vec& termConduction) const {
    const int nE = grid_.nElems();
    termElastic = Vec::Zero(nE);
    termThermalLoad = Vec::Zero(nE);
    termConduction = Vec::Zero(nE);

    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto nodes = grid_.elementNodes(ex, ey, ez);
                const auto dofs = grid_.elementDofs(ex, ey, ez);

                Eigen::Matrix<double, 24, 1> Ue, lE;
                for (int a = 0; a < 24; ++a) {
                    Ue(a) = U(dofs[static_cast<size_t>(a)]);
                    lE(a) = lamE(dofs[static_cast<size_t>(a)]);
                }
                Eigen::Matrix<double, 8, 1> Te, lT;
                for (int a = 0; a < 8; ++a) {
                    Te(a) = T(nodes[static_cast<size_t>(a)]);
                    lT(a) = lamT(nodes[static_cast<size_t>(a)]);
                }

                const double r = clamp01(rho(eid));
                const double dE =
                    mat_.p * std::pow(r, mat_.p - 1.0) * (mat_.E0 - mat_.Emin);
                const double dk =
                    mat_.q * std::pow(r, mat_.q - 1.0) * (mat_.k0 - mat_.kmin);

                // lam_e^T (dK_e/drho) U = dE * lam_e^T KE0 U
                const double te = dE * (lE.transpose() * ke0_ * Ue)(0, 0);
                // -lam_e^T (dF_th/drho) = -dE alpha lam_e^T C (T - Tref)
                Eigen::Matrix<double, 8, 1> dTe;
                for (int a = 0; a < 8; ++a) dTe(a) = Te(a) - mat_.Tref;
                const double tf =
                    -dE * mat_.alpha * (lE.transpose() * (cth_ * dTe))(0, 0);
                // lam_t^T (dK_t/drho) T = dk * lam_t^T L0 T
                const double tc = dk * (lT.transpose() * l0_ * Te)(0, 0);

                termElastic(eid) = te;
                termThermalLoad(eid) = tf;
                termConduction(eid) = tc;
            }
}

double ThermoElasticAdjoint::stressPNorm(const Vec& rho,
                                         const StressModel& sm) const {
    Vec T, U;
    forward(rho, T, U);
    const Vec vm0 = sm.vonMisesSolid(grid_, U);
    const Vec sigma = sm.relaxedStress(rho, vm0);
    return sm.pNorm(sigma);
}

ThermoElasticAdjoint::StressSolution ThermoElasticAdjoint::stressPNormGrad(
    const Vec& rho, const StressModel& sm) const {
    StressSolution sol;
    forward(rho, sol.T, sol.U);

    const double q = sm.qRelax();
    const double P = sm.Pagg();
    const auto& S0 = sm.S0();
    const auto& V = sm.V();
    const H8Element::Mat6 Vsym = 0.5 * (V + V.transpose());

    const Vec vm0 = sm.vonMisesSolid(grid_, sol.U);
    const Vec sigma = sm.relaxedStress(rho, vm0);
    const double sigPN = sm.pNorm(sigma);
    sol.J = sigPN;

    const int nE = grid_.nElems();
    const double vmFloor = 1e-12;  // guard vm0 ~ 0 in dvm0/du = (1/vm0) S0^T V s

    // Per-element dJ/dsigma_e = sigma_PN^(1-P) sigma_e^(P-1).
    Vec dJdsig(nE);
    for (int e = 0; e < nE; ++e)
        dJdsig(e) = std::pow(sigPN, 1.0 - P) * std::pow(sigma(e), P - 1.0);

    // dJ/dU = sum_e (dJ/dsigma_e)(dsigma_e/du_e), scattered to global DOFs,
    // with dsigma_e/du_e = rho_e^q (1/vm0_e) S0^T V s_e.
    // Explicit term dJ/drho_i|exp = (dJ/dsigma_i) q rho_i^(q-1) vm0_i.
    Vec dJdU = Vec::Zero(grid_.nDof());
    sol.termExplicit = Vec::Zero(nE);
    for (int ez = 0; ez < grid_.nelz(); ++ez)
        for (int ey = 0; ey < grid_.nely(); ++ey)
            for (int ex = 0; ex < grid_.nelx(); ++ex) {
                const int eid = grid_.elemId(ex, ey, ez);
                const auto dofs = grid_.elementDofs(ex, ey, ez);
                Eigen::Matrix<double, 24, 1> ue;
                for (int a = 0; a < 24; ++a)
                    ue(a) = sol.U(dofs[static_cast<size_t>(a)]);

                const double r = clamp01(rho(eid));
                const double rq = std::pow(r, q);

                if (vm0(eid) > vmFloor) {
                    const Eigen::Matrix<double, 6, 1> s = S0 * ue;
                    const Eigen::Matrix<double, 24, 1> dsig_du =
                        (rq / vm0(eid)) * (S0.transpose() * (Vsym * s));
                    const double w = dJdsig(eid);
                    for (int a = 0; a < 24; ++a)
                        dJdU(dofs[static_cast<size_t>(a)]) += w * dsig_du(a);
                }

                // Explicit term: rho^(q-1) can diverge for q<1, floor rho (LL-008).
                const double rEps = std::max(r, 1e-9);
                sol.termExplicit(eid) =
                    dJdsig(eid) * q * std::pow(rEps, q - 1.0) * vm0(eid);
            }

    // (1) Elastic adjoint with the stress RHS: K_e lam_e = -dJ/dU.
    const Vec E = youngModulus(rho);
    const Vec k = conductivity(rho);
    const Vec lamE = fem_.solve(E, -dJdU);

    // (2) Thermal adjoint: K_t lam_t = G^T lam_e (same coupling as compliance).
    const Vec lamT = solveThermal(k, thermalAdjointRhs(E, lamE));

    // (3) Three shared rho-derivative terms with the new adjoints.
    hereditaryGradient(rho, sol.U, sol.T, lamE, lamT, sol.termElastic,
                       sol.termThermalLoad, sol.termConduction);

    sol.grad = sol.termExplicit + sol.termElastic + sol.termThermalLoad +
               sol.termConduction;
    return sol;
}

} // namespace topopt
