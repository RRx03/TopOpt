#include "filter/Helmholtz3D.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "fem/H8Element.hpp"

namespace topopt {

namespace {
struct GridDims {
    std::uint32_t nelx, nely, nelz, nnodes;
};
constexpr unsigned TG = 256;
unsigned ceilDiv(unsigned a, unsigned b) { return (a + b - 1) / b; }
} // namespace

Helmholtz3D::Helmholtz3D(gpu::MetalContext& ctx, const Grid3D& grid,
                         double radiusCells)
    : ctx_(ctx), grid_(grid) {
    nNodes_ = static_cast<unsigned>(grid.nNodes());
    nElems_ = static_cast<unsigned>(grid.nElems());
    nPartials_ = ceilDiv(nNodes_, TG);

    if (!ctx_.loadLibrary("build/shaders.metallib")) return;
    pScatter_ = ctx_.makePipeline("helm_scatter");
    pGather_ = ctx_.makePipeline("helm_gather");
    pMatvec_ = ctx_.makePipeline("mf_matvec_helmholtz");
    pDiag_ = ctx_.makePipeline("mf_diag_helmholtz");
    pAxpy_ = ctx_.makePipeline("vec_axpy");
    pXpby_ = ctx_.makePipeline("vec_xpby");
    pCopy_ = ctx_.makePipeline("vec_copy");
    pDot_ = ctx_.makePipeline("vec_dot_partial");
    pPrecond_ = ctx_.makePipeline("precond_jacobi");
    if (!pScatter_ || !pGather_ || !pMatvec_ || !pDiag_ || !pAxpy_ || !pXpby_ ||
        !pCopy_ || !pDot_ || !pPrecond_)
        return;

    MTL::Device* d = ctx_.device();
    const auto mode = MTL::ResourceStorageModeShared;

    // KF0 = rlen^2 * Le + Me (8x8), rlen = R/(2*sqrt(3)).
    const double rlen = radiusCells / (2.0 * std::sqrt(3.0));
    const auto Le = H8Element::diffusion();
    const auto Me = H8Element::mass();
    bKF0_ = d->newBuffer(64 * sizeof(float), mode);
    float* kf = static_cast<float*>(bKF0_->contents());
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c)
            kf[r * 8 + c] = static_cast<float>(rlen * rlen * Le(r, c) + Me(r, c));

    bGrid_ = d->newBuffer(sizeof(GridDims), mode);
    GridDims gd{static_cast<std::uint32_t>(grid.nelx()),
               static_cast<std::uint32_t>(grid.nely()),
               static_cast<std::uint32_t>(grid.nelz()), nNodes_};
    std::memcpy(bGrid_->contents(), &gd, sizeof(gd));

    const std::size_t fNode = static_cast<std::size_t>(nNodes_) * sizeof(float);
    const std::size_t fElem = static_cast<std::size_t>(nElems_) * sizeof(float);
    bXe_ = d->newBuffer(fElem, mode);
    bOe_ = d->newBuffer(fElem, mode);
    bRhs_ = d->newBuffer(fNode, mode);
    bY_ = d->newBuffer(fNode, mode);
    bR_ = d->newBuffer(fNode, mode);
    bZ_ = d->newBuffer(fNode, mode);
    bP_ = d->newBuffer(fNode, mode);
    bQ_ = d->newBuffer(fNode, mode);
    bInvDiag_ = d->newBuffer(fNode, mode);
    bZeroFixed_ = d->newBuffer(static_cast<std::size_t>(nNodes_), mode);
    std::memset(bZeroFixed_->contents(), 0, nNodes_);
    bPartials_ = d->newBuffer(static_cast<std::size_t>(nPartials_) * sizeof(float), mode);

    // Precompute inverse diagonal (operator is design-independent).
    {
        MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
        MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
        enc->setComputePipelineState(pDiag_);
        enc->setBuffer(bKF0_, 0, 0);
        enc->setBuffer(bGrid_, 0, 1);
        enc->setBuffer(bInvDiag_, 0, 2);  // reuse as diag scratch first
        enc->dispatchThreads(MTL::Size(nNodes_, 1, 1), MTL::Size(TG, 1, 1));
        enc->endEncoding();
        cmd->commit();
        cmd->waitUntilCompleted();
        float* inv = static_cast<float*>(bInvDiag_->contents());
        for (unsigned i = 0; i < nNodes_; ++i)
            inv[i] = (inv[i] == 0.0f) ? 0.0f : 1.0f / inv[i];
    }
    ok_ = true;
}

Helmholtz3D::~Helmholtz3D() {
    MTL::Buffer* bufs[] = {bKF0_, bGrid_, bXe_, bOe_, bRhs_, bY_, bR_, bZ_,
                           bP_, bQ_, bInvDiag_, bZeroFixed_, bPartials_};
    for (MTL::Buffer* b : bufs)
        if (b) b->release();
    MTL::ComputePipelineState* psos[] = {pScatter_, pGather_, pMatvec_, pDiag_,
                                         pAxpy_, pXpby_, pCopy_, pDot_, pPrecond_};
    for (MTL::ComputePipelineState* p : psos)
        if (p) p->release();
}

void Helmholtz3D::runUnary(MTL::ComputePipelineState* pso, MTL::Buffer* in,
                           MTL::Buffer* out, unsigned nThreads,
                           bool /*nodalCount*/) const {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(in, 0, 0);
    enc->setBuffer(bGrid_, 0, 1);
    enc->setBuffer(out, 0, 2);
    enc->dispatchThreads(MTL::Size(nThreads, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

void Helmholtz3D::matvec(MTL::Buffer* y, MTL::Buffer* out) const {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pMatvec_);
    enc->setBuffer(y, 0, 0);
    enc->setBuffer(bKF0_, 0, 1);
    enc->setBuffer(bGrid_, 0, 2);
    enc->setBuffer(out, 0, 3);
    enc->dispatchThreads(MTL::Size(nNodes_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

void Helmholtz3D::axpy(MTL::Buffer* y, MTL::Buffer* x, float a) const {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pAxpy_);
    enc->setBuffer(y, 0, 0);
    enc->setBuffer(x, 0, 1);
    enc->setBytes(&a, sizeof(a), 2);
    enc->setBytes(&nNodes_, sizeof(nNodes_), 3);
    enc->dispatchThreads(MTL::Size(nNodes_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

void Helmholtz3D::xpby(MTL::Buffer* y, MTL::Buffer* x, float b) const {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pXpby_);
    enc->setBuffer(y, 0, 0);
    enc->setBuffer(x, 0, 1);
    enc->setBytes(&b, sizeof(b), 2);
    enc->setBytes(&nNodes_, sizeof(nNodes_), 3);
    enc->dispatchThreads(MTL::Size(nNodes_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

void Helmholtz3D::copy(MTL::Buffer* dst, MTL::Buffer* src) const {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pCopy_);
    enc->setBuffer(dst, 0, 0);
    enc->setBuffer(src, 0, 1);
    enc->setBytes(&nNodes_, sizeof(nNodes_), 2);
    enc->dispatchThreads(MTL::Size(nNodes_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

void Helmholtz3D::precond(MTL::Buffer* r, MTL::Buffer* z) const {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pPrecond_);
    enc->setBuffer(r, 0, 0);
    enc->setBuffer(bInvDiag_, 0, 1);
    enc->setBuffer(bZeroFixed_, 0, 2);
    enc->setBuffer(z, 0, 3);
    enc->setBytes(&nNodes_, sizeof(nNodes_), 4);
    enc->dispatchThreads(MTL::Size(nNodes_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

double Helmholtz3D::dot(MTL::Buffer* a, MTL::Buffer* b) const {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pDot_);
    enc->setBuffer(a, 0, 0);
    enc->setBuffer(b, 0, 1);
    enc->setBytes(&nNodes_, sizeof(nNodes_), 2);
    enc->setBuffer(bPartials_, 0, 3);
    enc->dispatchThreadgroups(MTL::Size(nPartials_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    const float* p = static_cast<const float*>(bPartials_->contents());
    double s = 0.0;
    for (unsigned i = 0; i < nPartials_; ++i) s += static_cast<double>(p[i]);
    return s;
}

Helmholtz3D::Vec Helmholtz3D::apply(const Vec& xe) const {
    Vec out = Vec::Zero(nElems_);
    if (!ok_) return out;

    // Upload element field (double -> float).
    float* xf = static_cast<float*>(bXe_->contents());
    for (unsigned e = 0; e < nElems_; ++e) xf[e] = static_cast<float>(xe(e));

    // rhs = scatter(xe) over nodes.
    runUnary(pScatter_, bXe_, bRhs_, nNodes_, true);

    // Solve KF y = rhs (Jacobi PCG, no Dirichlet).
    std::memset(bY_->contents(), 0, static_cast<std::size_t>(nNodes_) * sizeof(float));
    copy(bR_, bRhs_);
    const double r0 = std::sqrt(dot(bR_, bR_));
    if (r0 > 0.0) {
        precond(bR_, bZ_);
        copy(bP_, bZ_);
        double rz = dot(bR_, bZ_);
        const int maxIter = 2000;
        const double tol = 1e-7;
        for (int it = 1; it <= maxIter; ++it) {
            matvec(bP_, bQ_);
            const double pq = dot(bP_, bQ_);
            const double alpha = rz / pq;
            axpy(bY_, bP_, static_cast<float>(alpha));
            axpy(bR_, bQ_, static_cast<float>(-alpha));
            if (std::sqrt(dot(bR_, bR_)) / r0 < tol) break;
            precond(bR_, bZ_);
            const double rzNew = dot(bR_, bZ_);
            xpby(bP_, bZ_, static_cast<float>(rzNew / rz));
            rz = rzNew;
        }
    }

    // gather node field -> element field.
    runUnary(pGather_, bY_, bOe_, nElems_, false);
    const float* of = static_cast<const float*>(bOe_->contents());
    for (unsigned e = 0; e < nElems_; ++e) out(e) = static_cast<double>(of[e]);
    return out;
}

} // namespace topopt
