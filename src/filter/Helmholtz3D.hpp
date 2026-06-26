#pragma once

#include <Eigen/Core>

#include "core/Grid3D.hpp"
#include "gpu/MetalContext.hpp"

namespace MTL {
class Buffer;
class ComputePipelineState;
} // namespace MTL

namespace topopt {

// PDE density filter (Helmholtz, Lazarov-Sigmund 2011), GPU matrix-free.
// (-rlen^2 ∇^2 + 1) rho~ = rho, rlen = radiusCells / (2*sqrt(3)).
// The operator is design-independent (assembled once as the 8x8 KF0 stencil),
// solved per call with a scalar Jacobi-preconditioned CG. Radius is in CELLS
// (Phase 1/2 convention); Phase 3 switches to physical mm.
class Helmholtz3D {
public:
    using Vec = Eigen::VectorXd;

    Helmholtz3D(gpu::MetalContext& ctx, const Grid3D& grid, double radiusCells);
    ~Helmholtz3D();
    Helmholtz3D(const Helmholtz3D&) = delete;
    Helmholtz3D& operator=(const Helmholtz3D&) = delete;

    bool valid() const { return ok_; }

    // Filter an element field (nElems) -> filtered element field (nElems).
    Vec apply(const Vec& xe) const;

private:
    void runUnary(MTL::ComputePipelineState* pso, MTL::Buffer* in,
                  MTL::Buffer* out, unsigned nThreads, bool nodalCount) const;
    void axpy(MTL::Buffer* y, MTL::Buffer* x, float a) const;
    void xpby(MTL::Buffer* y, MTL::Buffer* x, float b) const;
    void copy(MTL::Buffer* dst, MTL::Buffer* src) const;
    void matvec(MTL::Buffer* y, MTL::Buffer* out) const;
    void precond(MTL::Buffer* r, MTL::Buffer* z) const;
    double dot(MTL::Buffer* a, MTL::Buffer* b) const;

    gpu::MetalContext& ctx_;
    Grid3D grid_;
    bool ok_ = false;
    unsigned nNodes_ = 0, nElems_ = 0, nPartials_ = 0;

    MTL::ComputePipelineState* pScatter_ = nullptr;
    MTL::ComputePipelineState* pGather_ = nullptr;
    MTL::ComputePipelineState* pMatvec_ = nullptr;
    MTL::ComputePipelineState* pDiag_ = nullptr;
    MTL::ComputePipelineState* pAxpy_ = nullptr;
    MTL::ComputePipelineState* pXpby_ = nullptr;
    MTL::ComputePipelineState* pCopy_ = nullptr;
    MTL::ComputePipelineState* pDot_ = nullptr;
    MTL::ComputePipelineState* pPrecond_ = nullptr;

    MTL::Buffer* bKF0_ = nullptr;
    MTL::Buffer* bGrid_ = nullptr;
    MTL::Buffer* bXe_ = nullptr;
    MTL::Buffer* bOe_ = nullptr;
    MTL::Buffer* bRhs_ = nullptr;
    MTL::Buffer* bY_ = nullptr;
    MTL::Buffer* bR_ = nullptr;
    MTL::Buffer* bZ_ = nullptr;
    MTL::Buffer* bP_ = nullptr;
    MTL::Buffer* bQ_ = nullptr;
    MTL::Buffer* bInvDiag_ = nullptr;
    MTL::Buffer* bZeroFixed_ = nullptr;  // all-zero mask for precond reuse
    MTL::Buffer* bPartials_ = nullptr;
};

} // namespace topopt
