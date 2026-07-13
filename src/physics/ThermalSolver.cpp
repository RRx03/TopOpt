#include "physics/ThermalSolver.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

namespace topopt::gpu {

namespace {
struct GridDims {
    std::uint32_t nelx, nely, nelz, nnodes;
};
constexpr unsigned TG = 256;
unsigned ceilDiv(unsigned a, unsigned b) { return (a + b - 1) / b; }
} // namespace

ThermalSolver::ThermalSolver(MetalContext& ctx, const Grid3D& grid,
                             const Eigen::Matrix<double, 8, 8>& L0)
    : ctx_(ctx), grid_(grid) {
    nNodes_ = static_cast<unsigned>(grid.nNodes());
    nElems_ = static_cast<unsigned>(grid.nElems());
    nPartials_ = ceilDiv(nNodes_, TG);

    if (!ctx_.loadLibrary("build/shaders.metallib")) return;
    pMatvec_ = ctx_.makePipeline("mf_matvec_thermal");
    pDiag_ = ctx_.makePipeline("mf_diag_thermal");
    pZeroFixed_ = ctx_.makePipeline("zero_fixed");
    pPrecond_ = ctx_.makePipeline("precond_jacobi");
    pAxpy_ = ctx_.makePipeline("vec_axpy");
    pXpby_ = ctx_.makePipeline("vec_xpby");
    pCopy_ = ctx_.makePipeline("vec_copy");
    pDot_ = ctx_.makePipeline("vec_dot_partial");
    if (!pMatvec_ || !pDiag_ || !pZeroFixed_ || !pPrecond_ || !pAxpy_ ||
        !pXpby_ || !pCopy_ || !pDot_)
        return;

    MTL::Device* d = ctx_.device();
    const auto mode = MTL::ResourceStorageModeShared;
    const std::size_t fNode = static_cast<std::size_t>(nNodes_) * sizeof(float);

    bL0_ = d->newBuffer(64 * sizeof(float), mode);
    float* l0 = static_cast<float*>(bL0_->contents());
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) l0[r * 8 + c] = static_cast<float>(L0(r, c));

    bGrid_ = d->newBuffer(sizeof(GridDims), mode);
    GridDims gd{static_cast<std::uint32_t>(grid.nelx()),
               static_cast<std::uint32_t>(grid.nely()),
               static_cast<std::uint32_t>(grid.nelz()), nNodes_};
    std::memcpy(bGrid_->contents(), &gd, sizeof(gd));

    bKc_ = d->newBuffer(static_cast<std::size_t>(nElems_) * sizeof(float), mode);
    bFixed_ = d->newBuffer(static_cast<std::size_t>(nNodes_) * sizeof(std::uint8_t), mode);
    bInvDiag_ = d->newBuffer(fNode, mode);
    bT_ = d->newBuffer(fNode, mode);
    bQ_ = d->newBuffer(fNode, mode);
    bR_ = d->newBuffer(fNode, mode);
    bZ_ = d->newBuffer(fNode, mode);
    bP_ = d->newBuffer(fNode, mode);
    bAp_ = d->newBuffer(fNode, mode);
    bDiag_ = d->newBuffer(fNode, mode);
    bPartials_ = d->newBuffer(static_cast<std::size_t>(nPartials_) * sizeof(float), mode);

    ok_ = true;
}

ThermalSolver::~ThermalSolver() {
    MTL::Buffer* bufs[] = {bL0_, bGrid_, bKc_, bFixed_, bInvDiag_, bT_, bQ_,
                           bR_, bZ_, bP_, bAp_, bDiag_, bPartials_};
    for (MTL::Buffer* b : bufs)
        if (b) b->release();
    MTL::ComputePipelineState* psos[] = {pMatvec_, pDiag_, pZeroFixed_, pPrecond_,
                                         pAxpy_, pXpby_, pCopy_, pDot_};
    for (MTL::ComputePipelineState* p : psos)
        if (p) p->release();
}

void ThermalSolver::matvec(MTL::Buffer* x, MTL::Buffer* y) {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pMatvec_);
    enc->setBuffer(x, 0, 0);
    enc->setBuffer(bKc_, 0, 1);
    enc->setBuffer(bL0_, 0, 2);
    enc->setBuffer(bGrid_, 0, 3);
    enc->setBuffer(y, 0, 4);
    enc->dispatchThreads(MTL::Size(nNodes_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    zeroFixed(y);
}

void ThermalSolver::zeroFixed(MTL::Buffer* x) {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pZeroFixed_);
    enc->setBuffer(x, 0, 0);
    enc->setBuffer(bFixed_, 0, 1);
    enc->setBytes(&nNodes_, sizeof(nNodes_), 2);
    enc->dispatchThreads(MTL::Size(nNodes_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

void ThermalSolver::precond(MTL::Buffer* r, MTL::Buffer* z) {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pPrecond_);
    enc->setBuffer(r, 0, 0);
    enc->setBuffer(bInvDiag_, 0, 1);
    enc->setBuffer(bFixed_, 0, 2);
    enc->setBuffer(z, 0, 3);
    enc->setBytes(&nNodes_, sizeof(nNodes_), 4);
    enc->dispatchThreads(MTL::Size(nNodes_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

void ThermalSolver::axpy(MTL::Buffer* y, MTL::Buffer* x, float a) {
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

void ThermalSolver::xpby(MTL::Buffer* y, MTL::Buffer* x, float b) {
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

void ThermalSolver::copy(MTL::Buffer* dst, MTL::Buffer* src) {
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

double ThermalSolver::dot(MTL::Buffer* a, MTL::Buffer* b) {
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

ThermalSolver::Result ThermalSolver::solve(const std::vector<float>& kcond,
                                           const std::vector<float>& Q,
                                           const std::vector<std::uint8_t>& fixedT,
                                           std::vector<float>& T, int maxIter,
                                           float tol) {
    Result res;
    if (!ok_) return res;

    std::memcpy(bKc_->contents(), kcond.data(), kcond.size() * sizeof(float));
    std::memcpy(bFixed_->contents(), fixedT.data(), fixedT.size());
    std::memcpy(bQ_->contents(), Q.data(), Q.size() * sizeof(float));
    std::memset(bT_->contents(), 0, static_cast<std::size_t>(nNodes_) * sizeof(float));

    // Diagonal + inverse (Jacobi).
    {
        MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
        MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
        enc->setComputePipelineState(pDiag_);
        enc->setBuffer(bKc_, 0, 0);
        enc->setBuffer(bL0_, 0, 1);
        enc->setBuffer(bGrid_, 0, 2);
        enc->setBuffer(bDiag_, 0, 3);
        enc->dispatchThreads(MTL::Size(nNodes_, 1, 1), MTL::Size(TG, 1, 1));
        enc->endEncoding();
        cmd->commit();
        cmd->waitUntilCompleted();
    }
    {
        const float* dg = static_cast<const float*>(bDiag_->contents());
        float* inv = static_cast<float*>(bInvDiag_->contents());
        for (unsigned i = 0; i < nNodes_; ++i)
            inv[i] = (fixedT[i] || dg[i] == 0.0f) ? 0.0f : 1.0f / dg[i];
    }

    // r = Q - K T, T=0 => r = Q (project fixed to 0).
    copy(bR_, bQ_);
    zeroFixed(bR_);
    const double r0 = std::sqrt(dot(bR_, bR_));
    if (r0 == 0.0) {
        std::memset(T.data(), 0, T.size() * sizeof(float));
        res.converged = true;
        return res;
    }

    precond(bR_, bZ_);
    copy(bP_, bZ_);
    double rz = dot(bR_, bZ_);

    int it = 0;
    double rel = 1.0;
    for (it = 1; it <= maxIter; ++it) {
        matvec(bP_, bAp_);
        const double pq = dot(bP_, bAp_);
        const double alpha = rz / pq;
        axpy(bT_, bP_, static_cast<float>(alpha));
        axpy(bR_, bAp_, static_cast<float>(-alpha));
        rel = std::sqrt(dot(bR_, bR_)) / r0;
        if (rel < tol) break;
        precond(bR_, bZ_);
        const double rzNew = dot(bR_, bZ_);
        xpby(bP_, bZ_, static_cast<float>(rzNew / rz));
        rz = rzNew;
    }

    std::memcpy(T.data(), bT_->contents(),
                static_cast<std::size_t>(nNodes_) * sizeof(float));
    res.iters = it;
    res.relResidual = static_cast<float>(rel);
    res.converged = rel < tol;
    return res;
}

} // namespace topopt::gpu
