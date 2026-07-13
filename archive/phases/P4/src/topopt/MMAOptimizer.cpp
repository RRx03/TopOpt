#include "topopt/MMAOptimizer.hpp"

#include <algorithm>
#include <cmath>

#include <Eigen/Cholesky>

namespace topopt {

namespace {
inline double clampd(double v, double lo, double hi) {
    return std::max(lo, std::min(hi, v));
}
} // namespace

MMAOptimizer::MMAOptimizer(int n, int m, const Params& p)
    : n_(n), m_(m), prm_(p) {
    low_  = Vec::Zero(n_);
    upp_  = Vec::Zero(n_);
    xold1_ = Vec::Zero(n_);
    xold2_ = Vec::Zero(n_);
    p_ = Mat::Zero(m_ + 1, n_);
    q_ = Mat::Zero(m_ + 1, n_);
    r_ = Vec::Zero(m_ + 1);
    alpha_ = Vec::Zero(n_);
    beta_  = Vec::Zero(n_);
    a_ = Vec::Constant(m_, prm_.aDefault);
    c_ = Vec::Constant(m_, prm_.cDefault);
    d_ = Vec::Constant(m_, prm_.dDefault);
}

void MMAOptimizer::updateAsymptotes(const Vec& x, const Vec& xmin,
                                    const Vec& xmax) {
    const Vec range = xmax - xmin;
    if (iter_ <= 1) {
        low_ = x - prm_.asyinit * range;
        upp_ = x + prm_.asyinit * range;
    } else {
        for (int j = 0; j < n_; ++j) {
            const double s = (x(j) - xold1_(j)) * (xold1_(j) - xold2_(j));
            double gamma = 1.0;
            if (s < 0.0) gamma = prm_.asydecr;
            else if (s > 0.0) gamma = prm_.asyincr;

            low_(j) = x(j) - gamma * (xold1_(j) - low_(j));
            upp_(j) = x(j) + gamma * (upp_(j) - xold1_(j));

            // Bound the asymptote distances to [0.01, 10] * range (Svanberg).
            const double lmin = x(j) - 10.0 * range(j);
            const double lmax = x(j) - 0.01 * range(j);
            const double umin = x(j) + 0.01 * range(j);
            const double umax = x(j) + 10.0 * range(j);
            low_(j) = clampd(low_(j), lmin, lmax);
            upp_(j) = clampd(upp_(j), umin, umax);
        }
    }
}

void MMAOptimizer::buildSubproblem(const Vec& x, double f0, const Vec& df0dx,
                                   const Vec& fvals, const Mat& dfdx,
                                   const Vec& xmin, const Vec& xmax) {
    const Vec range = xmax - xmin;

    // Move limits / asymptote-relative bounds.
    for (int j = 0; j < n_; ++j) {
        const double alo = std::max({xmin(j),
                                     low_(j) + prm_.albefa * (x(j) - low_(j)),
                                     x(j) - prm_.move * range(j)});
        const double bhi = std::min({xmax(j),
                                     upp_(j) - prm_.albefa * (upp_(j) - x(j)),
                                     x(j) + prm_.move * range(j)});
        alpha_(j) = alo;
        beta_(j)  = std::max(bhi, alo);  // guard alpha <= beta
    }

    // p_ij / q_ij and r_i for every function (row 0 = objective).
    r_.setZero();
    for (int i = 0; i <= m_; ++i) {
        const double fi = (i == 0) ? f0 : fvals(i - 1);
        double ri = fi;
        for (int j = 0; j < n_; ++j) {
            const double dfi = (i == 0) ? df0dx(j) : dfdx(i - 1, j);
            const double ux = upp_(j) - x(j);   // > 0 by construction
            const double xl = x(j) - low_(j);   // > 0 by construction
            const double dplus  = std::max(dfi, 0.0);
            const double dminus = std::max(-dfi, 0.0);
            const double reg = 1e-5 / std::max(range(j), 1e-12);
            const double pij = ux * ux * (1.001 * dplus + 0.001 * dminus + reg);
            const double qij = xl * xl * (0.001 * dplus + 1.001 * dminus + reg);
            p_(i, j) = pij;
            q_(i, j) = qij;
            ri -= pij / ux + qij / xl;
        }
        r_(i) = ri;
    }
}

void MMAOptimizer::primalX(const Vec& lambda, Vec& x) const {
    x.resize(n_);
    for (int j = 0; j < n_; ++j) {
        double Pj = p_(0, j);
        double Qj = q_(0, j);
        for (int i = 0; i < m_; ++i) {
            Pj += lambda(i) * p_(i + 1, j);
            Qj += lambda(i) * q_(i + 1, j);
        }
        Pj = std::max(Pj, 1e-30);   // strictly positive (LL-008)
        Qj = std::max(Qj, 1e-30);
        const double sp = std::sqrt(Pj);
        const double sq = std::sqrt(Qj);
        double xj = (sp * low_(j) + sq * upp_(j)) / (sp + sq);
        x(j) = clampd(xj, alpha_(j), beta_(j));
    }
}

double MMAOptimizer::dualValue(const Vec& lambda) const {
    Vec x;
    primalX(lambda, x);
    double W = r_(0);
    for (int i = 0; i < m_; ++i) W += lambda(i) * r_(i + 1);
    for (int j = 0; j < n_; ++j) {
        double Pj = p_(0, j);
        double Qj = q_(0, j);
        for (int i = 0; i < m_; ++i) {
            Pj += lambda(i) * p_(i + 1, j);
            Qj += lambda(i) * q_(i + 1, j);
        }
        W += Pj / (upp_(j) - x(j)) + Qj / (x(j) - low_(j));
    }
    for (int i = 0; i < m_; ++i) {
        const double yi = std::max(0.0, (lambda(i) - c_(i)) / d_(i));
        W += c_(i) * yi + 0.5 * d_(i) * yi * yi - lambda(i) * yi;
    }
    return W;
}

MMAOptimizer::Vec MMAOptimizer::dualGrad(const Vec& lambda, const Vec& x) const {
    Vec g(m_);
    for (int i = 0; i < m_; ++i) {
        double gi = r_(i + 1);
        for (int j = 0; j < n_; ++j)
            gi += p_(i + 1, j) / (upp_(j) - x(j)) + q_(i + 1, j) / (x(j) - low_(j));
        const double yi = std::max(0.0, (lambda(i) - c_(i)) / d_(i));
        g(i) = gi - yi;
    }
    return g;
}

MMAOptimizer::Mat MMAOptimizer::dualHess(const Vec& lambda, const Vec& x) const {
    // dx_j/dlambda_k for interior (unclamped) variables; 0 if clamped.
    Mat dxdl = Mat::Zero(n_, m_);
    const double eps = 1e-12;
    for (int j = 0; j < n_; ++j) {
        const bool interior = (x(j) > alpha_(j) + eps) && (x(j) < beta_(j) - eps);
        if (!interior) continue;
        double Pj = p_(0, j);
        double Qj = q_(0, j);
        for (int i = 0; i < m_; ++i) {
            Pj += lambda(i) * p_(i + 1, j);
            Qj += lambda(i) * q_(i + 1, j);
        }
        Pj = std::max(Pj, 1e-30);
        Qj = std::max(Qj, 1e-30);
        const double sp = std::sqrt(Pj);
        const double sq = std::sqrt(Qj);
        const double denom = sp + sq;
        for (int k = 0; k < m_; ++k) {
            const double dsp = p_(k + 1, j) / (2.0 * sp);
            const double dsq = q_(k + 1, j) / (2.0 * sq);
            dxdl(j, k) = (dsp * (low_(j) - x(j)) + dsq * (upp_(j) - x(j))) / denom;
        }
    }

    Mat H = Mat::Zero(m_, m_);
    for (int i = 0; i < m_; ++i) {
        for (int k = 0; k < m_; ++k) {
            double h = 0.0;
            for (int j = 0; j < n_; ++j) {
                const double ux = upp_(j) - x(j);
                const double xl = x(j) - low_(j);
                const double dgi = p_(i + 1, j) / (ux * ux)
                                 - q_(i + 1, j) / (xl * xl);
                h += dgi * dxdl(j, k);
            }
            H(i, k) = h;
        }
        const double yi = std::max(0.0, (lambda(i) - c_(i)) / d_(i));
        if (yi > 0.0) H(i, i) -= 1.0 / d_(i);  // dy_i/dlambda_i active
    }
    return H;
}

MMAOptimizer::Vec MMAOptimizer::solveDual() const {
    if (m_ == 1) {
        // 1-D concave maximisation: dW/dlambda is decreasing. Bracket then bisect.
        Vec lam(1);
        lam(0) = 0.0;
        Vec x;
        primalX(lam, x);
        double g0 = dualGrad(lam, x)(0);
        if (g0 <= 0.0) return lam;  // constraint inactive at lambda = 0

        double lo = 0.0, hi = 1.0;
        int guard = 0;
        for (; guard < 200; ++guard) {
            lam(0) = hi;
            primalX(lam, x);
            if (dualGrad(lam, x)(0) < 0.0) break;
            lo = hi;
            hi *= 2.0;
        }
        for (int it = 0; it < prm_.maxDualIter; ++it) {
            const double mid = 0.5 * (lo + hi);
            lam(0) = mid;
            primalX(lam, x);
            const double g = dualGrad(lam, x)(0);
            if (g > 0.0) lo = mid; else hi = mid;
            if (hi - lo < prm_.dualTol * (1.0 + hi)) break;
        }
        lam(0) = 0.5 * (lo + hi);
        return lam;
    }

    // m >= 2: projected Newton (bounded, with backtracking on W).
    Vec lambda = Vec::Constant(m_, 1.0);
    Vec x;
    for (int it = 0; it < prm_.maxDualIter; ++it) {
        primalX(lambda, x);
        const Vec g = dualGrad(lambda, x);

        // Projected-gradient stationarity: free if lambda>0 or pushing inward.
        double res = 0.0;
        for (int i = 0; i < m_; ++i) {
            const double gi = (lambda(i) <= 0.0 && g(i) < 0.0) ? 0.0 : g(i);
            res = std::max(res, std::abs(gi));
        }
        if (res < 1e-9) break;

        Mat H = dualHess(lambda, x);
        // Maximise: ascent step delta = (-H)^{-1} g, regularised SPD.
        Mat A = -H;
        for (int i = 0; i < m_; ++i) A(i, i) += 1e-9;
        Vec delta = A.ldlt().solve(g);
        if (!delta.allFinite()) delta = g;  // fall back to gradient ascent

        const double W0 = dualValue(lambda);
        double t = 1.0;
        Vec lamNew = lambda;
        bool improved = false;
        for (int ls = 0; ls < 30; ++ls) {
            lamNew = (lambda + t * delta).cwiseMax(0.0);
            if (dualValue(lamNew) > W0) { improved = true; break; }
            t *= 0.5;
        }
        if (!improved) {
            // Pure projected gradient fallback.
            t = 1.0;
            for (int ls = 0; ls < 40; ++ls) {
                lamNew = (lambda + t * g).cwiseMax(0.0);
                if (dualValue(lamNew) > W0) { improved = true; break; }
                t *= 0.5;
            }
            if (!improved) break;
        }
        lambda = lamNew;
    }
    return lambda;
}

MMAOptimizer::Vec MMAOptimizer::step(const Vec& x, double f0, const Vec& df0dx,
                                     const Vec& fvals, const Mat& dfdx,
                                     const Vec& xmin, const Vec& xmax) {
    updateAsymptotes(x, xmin, xmax);
    buildSubproblem(x, f0, df0dx, fvals, dfdx, xmin, xmax);

    const Vec lambda = solveDual();
    Vec xnew;
    primalX(lambda, xnew);

    xold2_ = xold1_;
    xold1_ = x;
    ++iter_;
    return xnew;
}

} // namespace topopt
