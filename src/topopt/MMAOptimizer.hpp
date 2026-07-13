#pragma once

#include <Eigen/Core>

namespace topopt {

// Method of Moving Asymptotes (Svanberg 1987, the standard `mmasub`).
//
// Generic separable-convex optimiser: minimise f0(x) subject to f_i(x) <= 0,
// i = 1..m, and xmin <= x <= xmax, for n design variables. One call to step()
// solves one MMA subproblem and returns the next iterate; the caller loops it.
//
// The subproblem is solved by the DUAL method (recommended for small m). The
// primal minimiser is closed form per variable,
//     x_j(lambda) = (sqrt(P_j) L_j + sqrt(Q_j) U_j) / (sqrt(P_j) + sqrt(Q_j)),
// clamped to [alpha_j, beta_j], with P_j = p0_j + sum_i lambda_i p_ij and
// Q_j = q0_j + sum_i lambda_i q_ij. The concave dual W(lambda) is maximised
// over lambda >= 0: bisection when m == 1, projected Newton otherwise. All
// inner loops are capped (LL-008).
class MMAOptimizer {
public:
    using Vec = Eigen::VectorXd;
    using Mat = Eigen::MatrixXd;

    struct Params {
        double asyinit = 0.5;    // initial asymptote half-span (fraction of range)
        double asyincr = 1.2;    // monotone -> widen
        double asydecr = 0.7;    // oscillating -> tighten
        double albefa  = 0.1;    // bound: alpha = L + albefa*(x-L)
        double move    = 0.5;    // move limit (fraction of xmax-xmin)
        double a0      = 1.0;
        double aDefault = 0.0;   // per-constraint a_i
        double cDefault = 1000.0;// per-constraint c_i
        double dDefault = 1.0;   // per-constraint d_i
        int    maxDualIter = 200;
        double dualTol = 1e-12;
    };

    MMAOptimizer(int n, int m, const Params& p);

    // One MMA step. dfdx is m-by-n (row i = gradient of constraint f_i).
    // Returns the new design point.
    Vec step(const Vec& x, double f0, const Vec& df0dx,
             const Vec& fvals, const Mat& dfdx,
             const Vec& xmin, const Vec& xmax);

    int iter() const { return iter_; }
    const Vec& low() const { return low_; }
    const Vec& upp() const { return upp_; }

private:
    void updateAsymptotes(const Vec& x, const Vec& xmin, const Vec& xmax);
    void buildSubproblem(const Vec& x, double f0, const Vec& df0dx,
                         const Vec& fvals, const Mat& dfdx,
                         const Vec& xmin, const Vec& xmax);

    // Dual-method pieces (use the cached subproblem data p_/q_/r_, low_/upp_,
    // alpha_/beta_, a_/c_/d_).
    void   primalX(const Vec& lambda, Vec& x) const;   // x_j(lambda), clamped
    double dualValue(const Vec& lambda) const;          // W(lambda)
    Vec    dualGrad(const Vec& lambda, const Vec& x) const;
    Mat    dualHess(const Vec& lambda, const Vec& x) const;
    Vec    solveDual() const;                           // argmax_{lambda>=0} W

    int n_;
    int m_;
    Params prm_;
    int iter_ = 0;

    // History.
    Vec xold1_, xold2_;
    Vec low_, upp_;

    // Cached subproblem (rebuilt each step). Rows 0..m: 0 = objective.
    Mat p_, q_;        // (m+1) x n
    Vec r_;            // (m+1)
    Vec alpha_, beta_; // n
    Vec a_, c_, d_;    // m
};

} // namespace topopt
