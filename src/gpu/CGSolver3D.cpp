#include "gpu/CGSolver3D.hpp"

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

CGSolver3D::CGSolver3D(MetalContext& ctx, const Grid3D& grid,
                       const Eigen::Matrix<double, 24, 24>& KE0)
    : ctx_(ctx), grid_(grid) {
    nDof_ = static_cast<unsigned>(grid.nDof());
    nElems_ = static_cast<unsigned>(grid.nElems());
    nNodes_ = static_cast<unsigned>(grid.nNodes());
    nPartials_ = ceilDiv(nDof_, TG);

    if (!ctx_.loadLibrary("build/shaders.metallib")) return;
    pMatvec_ = ctx_.makePipeline("mf_matvec_elastic");
    pDiag_ = ctx_.makePipeline("mf_diag_elastic");
    pZeroFixed_ = ctx_.makePipeline("zero_fixed");
    pPrecond_ = ctx_.makePipeline("precond_jacobi");
    pAxpy_ = ctx_.makePipeline("vec_axpy");
    pXpby_ = ctx_.makePipeline("vec_xpby");
    pCopy_ = ctx_.makePipeline("vec_copy");
    pDot_ = ctx_.makePipeline("vec_dot_partial");
    pSE_ = ctx_.makePipeline("mf_strain_energy");
    if (!pMatvec_ || !pDiag_ || !pZeroFixed_ || !pPrecond_ || !pAxpy_ ||
        !pXpby_ || !pCopy_ || !pDot_ || !pSE_)
        return;

    MTL::Device* d = ctx_.device();
    const auto mode = MTL::ResourceStorageModeShared;
    const std::size_t fDof = static_cast<std::size_t>(nDof_) * sizeof(float);

    // KE0 as row-major float[576].
    bKE0_ = d->newBuffer(576 * sizeof(float), mode);
    float* ke = static_cast<float*>(bKE0_->contents());
    for (int r = 0; r < 24; ++r)
        for (int c = 0; c < 24; ++c)
            ke[r * 24 + c] = static_cast<float>(KE0(r, c));

    bGrid_ = d->newBuffer(sizeof(GridDims), mode);
    GridDims gd{static_cast<std::uint32_t>(grid.nelx()),
               static_cast<std::uint32_t>(grid.nely()),
               static_cast<std::uint32_t>(grid.nelz()), nNodes_};
    std::memcpy(bGrid_->contents(), &gd, sizeof(gd));

    bEmod_ = d->newBuffer(static_cast<std::size_t>(nElems_) * sizeof(float), mode);
    bFixed_ = d->newBuffer(static_cast<std::size_t>(nDof_) * sizeof(std::uint8_t), mode);
    bDiag_ = d->newBuffer(fDof, mode);
    bInvDiag_ = d->newBuffer(fDof, mode);
    bU_ = d->newBuffer(fDof, mode);
    bF_ = d->newBuffer(fDof, mode);
    bR_ = d->newBuffer(fDof, mode);
    bZ_ = d->newBuffer(fDof, mode);
    bP_ = d->newBuffer(fDof, mode);
    bQ_ = d->newBuffer(fDof, mode);
    bCe_ = d->newBuffer(static_cast<std::size_t>(nElems_) * sizeof(float), mode);
    bPartials_ = d->newBuffer(static_cast<std::size_t>(nPartials_) * sizeof(float), mode);

    ok_ = true;
}

CGSolver3D::~CGSolver3D() {
    MTL::Buffer* bufs[] = {bKE0_, bGrid_, bEmod_, bFixed_, bDiag_, bInvDiag_,
                           bU_, bF_, bR_, bZ_, bP_, bQ_, bCe_, bPartials_};
    for (MTL::Buffer* b : bufs)
        if (b) b->release();
    MTL::ComputePipelineState* psos[] = {pMatvec_, pDiag_, pZeroFixed_, pPrecond_,
                                         pAxpy_, pXpby_, pCopy_, pDot_, pSE_};
    for (MTL::ComputePipelineState* p : psos)
        if (p) p->release();
}

void CGSolver3D::matvec(MTL::Buffer* x, MTL::Buffer* y) {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pMatvec_);
    enc->setBuffer(x, 0, 0);
    enc->setBuffer(bEmod_, 0, 1);
    enc->setBuffer(bKE0_, 0, 2);
    enc->setBuffer(bGrid_, 0, 3);
    enc->setBuffer(y, 0, 4);
    enc->dispatchThreads(MTL::Size(nNodes_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    zeroFixed(y);
}

void CGSolver3D::zeroFixed(MTL::Buffer* x) {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pZeroFixed_);
    enc->setBuffer(x, 0, 0);
    enc->setBuffer(bFixed_, 0, 1);
    enc->setBytes(&nDof_, sizeof(nDof_), 2);
    enc->dispatchThreads(MTL::Size(nDof_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

void CGSolver3D::precond(MTL::Buffer* r, MTL::Buffer* z) {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pPrecond_);
    enc->setBuffer(r, 0, 0);
    enc->setBuffer(bInvDiag_, 0, 1);
    enc->setBuffer(bFixed_, 0, 2);
    enc->setBuffer(z, 0, 3);
    enc->setBytes(&nDof_, sizeof(nDof_), 4);
    enc->dispatchThreads(MTL::Size(nDof_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

void CGSolver3D::axpy(MTL::Buffer* y, MTL::Buffer* x, float a) {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pAxpy_);
    enc->setBuffer(y, 0, 0);
    enc->setBuffer(x, 0, 1);
    enc->setBytes(&a, sizeof(a), 2);
    enc->setBytes(&nDof_, sizeof(nDof_), 3);
    enc->dispatchThreads(MTL::Size(nDof_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

void CGSolver3D::xpby(MTL::Buffer* y, MTL::Buffer* x, float b) {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pXpby_);
    enc->setBuffer(y, 0, 0);
    enc->setBuffer(x, 0, 1);
    enc->setBytes(&b, sizeof(b), 2);
    enc->setBytes(&nDof_, sizeof(nDof_), 3);
    enc->dispatchThreads(MTL::Size(nDof_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

void CGSolver3D::copy(MTL::Buffer* dst, MTL::Buffer* src) {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pCopy_);
    enc->setBuffer(dst, 0, 0);
    enc->setBuffer(src, 0, 1);
    enc->setBytes(&nDof_, sizeof(nDof_), 2);
    enc->dispatchThreads(MTL::Size(nDof_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
}

double CGSolver3D::dot(MTL::Buffer* a, MTL::Buffer* b) {
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pDot_);
    enc->setBuffer(a, 0, 0);
    enc->setBuffer(b, 0, 1);
    enc->setBytes(&nDof_, sizeof(nDof_), 2);
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

CGSolver3D::Result CGSolver3D::solve(const std::vector<float>& Emod,
                                     const std::vector<float>& F,
                                     const std::vector<std::uint8_t>& fixed,
                                     std::vector<float>& U, int maxIter,
                                     float tol) {
    Result res;
    if (!ok_) return res;

    // Upload inputs.
    std::memcpy(bEmod_->contents(), Emod.data(), Emod.size() * sizeof(float));
    std::memcpy(bFixed_->contents(), fixed.data(), fixed.size());
    std::memcpy(bF_->contents(), F.data(), F.size() * sizeof(float));
    std::memset(bU_->contents(), 0, static_cast<std::size_t>(nDof_) * sizeof(float));

    // Diagonal + inverse diagonal (Jacobi).
    {
        MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
        MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
        enc->setComputePipelineState(pDiag_);
        enc->setBuffer(bEmod_, 0, 0);
        enc->setBuffer(bKE0_, 0, 1);
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
        for (unsigned i = 0; i < nDof_; ++i)
            inv[i] = (fixed[i] || dg[i] == 0.0f) ? 0.0f : 1.0f / dg[i];
    }

    // r = F - K u, with u = 0  =>  r = F (then project fixed to 0).
    copy(bR_, bF_);
    zeroFixed(bR_);

    const double r0 = std::sqrt(dot(bR_, bR_));
    if (r0 == 0.0) {
        std::memset(U.data(), 0, U.size() * sizeof(float));
        res.converged = true;
        return res;
    }

    precond(bR_, bZ_);          // z = M^-1 r
    copy(bP_, bZ_);             // p = z
    double rz = dot(bR_, bZ_);

    int it = 0;
    double rel = 1.0;
    for (it = 1; it <= maxIter; ++it) {
        matvec(bP_, bQ_);                       // q = K p
        const double pq = dot(bP_, bQ_);
        const double alpha = rz / pq;
        axpy(bU_, bP_, static_cast<float>(alpha));     // u += alpha p
        axpy(bR_, bQ_, static_cast<float>(-alpha));    // r -= alpha q

        rel = std::sqrt(dot(bR_, bR_)) / r0;
        if (rel < tol) break;

        precond(bR_, bZ_);                       // z = M^-1 r
        const double rzNew = dot(bR_, bZ_);
        const double beta = rzNew / rz;
        xpby(bP_, bZ_, static_cast<float>(beta));      // p = z + beta p
        rz = rzNew;
    }

    std::memcpy(U.data(), bU_->contents(),
                static_cast<std::size_t>(nDof_) * sizeof(float));
    res.iters = it;
    res.relResidual = static_cast<float>(rel);
    res.converged = rel < tol;
    return res;
}

void CGSolver3D::strainEnergy(std::vector<float>& ce) {
    if (!ok_) return;
    MTL::CommandBuffer* cmd = ctx_.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pSE_);
    enc->setBuffer(bU_, 0, 0);
    enc->setBuffer(bKE0_, 0, 1);
    enc->setBuffer(bGrid_, 0, 2);
    enc->setBuffer(bCe_, 0, 3);
    enc->dispatchThreads(MTL::Size(nElems_, 1, 1), MTL::Size(TG, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    ce.resize(nElems_);
    std::memcpy(ce.data(), bCe_->contents(),
                static_cast<std::size_t>(nElems_) * sizeof(float));
}

} // namespace topopt::gpu
