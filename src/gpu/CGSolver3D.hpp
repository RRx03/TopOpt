#pragma once

#include <cstdint>
#include <vector>

#include <Eigen/Core>

#include "core/Grid3D.hpp"
#include "gpu/MetalContext.hpp"

namespace MTL {
class Buffer;
class ComputePipelineState;
} // namespace MTL

namespace topopt::gpu {

// Matrix-free preconditioned Conjugate Gradient for 3D linear elasticity on a
// structured H8 grid, running on the GPU via Metal. K is never assembled:
// K*u is recomputed per node from the shared unit-modulus element matrix KE0,
// scaled by the per-element modulus. Jacobi (diagonal) preconditioner.
// Homogeneous Dirichlet BCs handled by projecting fixed DOFs to zero.
class CGSolver3D {
public:
    struct Result {
        int iters = 0;
        float relResidual = 0.0f;
        bool converged = false;
    };

    CGSolver3D(MetalContext& ctx, const Grid3D& grid,
               const Eigen::Matrix<double, 24, 24>& KE0);
    ~CGSolver3D();
    CGSolver3D(const CGSolver3D&) = delete;
    CGSolver3D& operator=(const CGSolver3D&) = delete;

    bool valid() const { return ok_; }

    // Solve K(Emod) U = F. Emod: nElems, F: nDof, fixed: nDof mask (1=fixed).
    // Writes U (nDof). tol is on ||r||/||r0||.
    Result solve(const std::vector<float>& Emod, const std::vector<float>& F,
                 const std::vector<std::uint8_t>& fixed, std::vector<float>& U,
                 int maxIter, float tol);

    // Per-element unit-modulus strain energy ce = u_e^T KE0 u_e from the last
    // solved displacement field. ce sized to nElems.
    void strainEnergy(std::vector<float>& ce);

private:
    void matvec(MTL::Buffer* x, MTL::Buffer* y);   // y = K x, fixed DOFs zeroed
    void axpy(MTL::Buffer* y, MTL::Buffer* x, float a);   // y += a x
    void xpby(MTL::Buffer* y, MTL::Buffer* x, float b);   // y = x + b y
    void copy(MTL::Buffer* dst, MTL::Buffer* src);
    void zeroFixed(MTL::Buffer* x);
    void precond(MTL::Buffer* r, MTL::Buffer* z);   // z = M^-1 r
    double dot(MTL::Buffer* a, MTL::Buffer* b);

    MetalContext& ctx_;
    Grid3D grid_;
    bool ok_ = false;

    unsigned nDof_ = 0, nElems_ = 0, nNodes_ = 0, nPartials_ = 0;

    // pipelines
    MTL::ComputePipelineState* pMatvec_ = nullptr;
    MTL::ComputePipelineState* pDiag_ = nullptr;
    MTL::ComputePipelineState* pZeroFixed_ = nullptr;
    MTL::ComputePipelineState* pPrecond_ = nullptr;
    MTL::ComputePipelineState* pAxpy_ = nullptr;
    MTL::ComputePipelineState* pXpby_ = nullptr;
    MTL::ComputePipelineState* pCopy_ = nullptr;
    MTL::ComputePipelineState* pDot_ = nullptr;
    MTL::ComputePipelineState* pSE_ = nullptr;

    // persistent buffers
    MTL::Buffer* bKE0_ = nullptr;
    MTL::Buffer* bGrid_ = nullptr;
    MTL::Buffer* bEmod_ = nullptr;
    MTL::Buffer* bFixed_ = nullptr;
    MTL::Buffer* bDiag_ = nullptr;
    MTL::Buffer* bInvDiag_ = nullptr;
    MTL::Buffer* bU_ = nullptr;
    MTL::Buffer* bF_ = nullptr;
    MTL::Buffer* bR_ = nullptr;
    MTL::Buffer* bZ_ = nullptr;
    MTL::Buffer* bP_ = nullptr;
    MTL::Buffer* bQ_ = nullptr;
    MTL::Buffer* bCe_ = nullptr;
    MTL::Buffer* bPartials_ = nullptr;
};

} // namespace topopt::gpu
