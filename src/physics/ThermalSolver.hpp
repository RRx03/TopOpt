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

// Matrix-free steady-state heat conduction solver on the GPU:
//   -div(k grad T) = q,  homogeneous Dirichlet on fixed nodes.
// Scalar field (1 DOF/node). Per-element conductivity scales the shared 8x8
// element Laplacian L0 (H8Element::diffusion()). Jacobi-preconditioned CG,
// same machinery as the elastic CGSolver3D but scalar.
class ThermalSolver {
public:
    struct Result {
        int iters = 0;
        float relResidual = 0.0f;
        bool converged = false;
    };

    ThermalSolver(MetalContext& ctx, const Grid3D& grid,
                  const Eigen::Matrix<double, 8, 8>& L0);
    ~ThermalSolver();
    ThermalSolver(const ThermalSolver&) = delete;
    ThermalSolver& operator=(const ThermalSolver&) = delete;

    bool valid() const { return ok_; }

    // Solve K(kcond) T = Q. kcond: nElems conductivities, Q: nNodes source,
    // fixedT: nNodes mask (1 = Dirichlet T=0). Writes T (nNodes).
    Result solve(const std::vector<float>& kcond, const std::vector<float>& Q,
                 const std::vector<std::uint8_t>& fixedT, std::vector<float>& T,
                 int maxIter, float tol);

private:
    void matvec(MTL::Buffer* x, MTL::Buffer* y);
    void zeroFixed(MTL::Buffer* x);
    void precond(MTL::Buffer* r, MTL::Buffer* z);
    void axpy(MTL::Buffer* y, MTL::Buffer* x, float a);
    void xpby(MTL::Buffer* y, MTL::Buffer* x, float b);
    void copy(MTL::Buffer* dst, MTL::Buffer* src);
    double dot(MTL::Buffer* a, MTL::Buffer* b);

    MetalContext& ctx_;
    Grid3D grid_;
    bool ok_ = false;
    unsigned nNodes_ = 0, nElems_ = 0, nPartials_ = 0;

    MTL::ComputePipelineState* pMatvec_ = nullptr;
    MTL::ComputePipelineState* pDiag_ = nullptr;
    MTL::ComputePipelineState* pZeroFixed_ = nullptr;
    MTL::ComputePipelineState* pPrecond_ = nullptr;
    MTL::ComputePipelineState* pAxpy_ = nullptr;
    MTL::ComputePipelineState* pXpby_ = nullptr;
    MTL::ComputePipelineState* pCopy_ = nullptr;
    MTL::ComputePipelineState* pDot_ = nullptr;

    MTL::Buffer* bL0_ = nullptr;
    MTL::Buffer* bGrid_ = nullptr;
    MTL::Buffer* bKc_ = nullptr;
    MTL::Buffer* bFixed_ = nullptr;
    MTL::Buffer* bInvDiag_ = nullptr;
    MTL::Buffer* bT_ = nullptr;
    MTL::Buffer* bQ_ = nullptr;
    MTL::Buffer* bR_ = nullptr;
    MTL::Buffer* bZ_ = nullptr;
    MTL::Buffer* bP_ = nullptr;
    MTL::Buffer* bAp_ = nullptr;
    MTL::Buffer* bDiag_ = nullptr;
    MTL::Buffer* bPartials_ = nullptr;
};

} // namespace topopt::gpu
