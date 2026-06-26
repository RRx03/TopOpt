// Metal hello-world: report device capabilities, then add two 1M-element
// float vectors on the GPU and check the result against a CPU reference.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "gpu/MetalContext.hpp"

int main() {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    topopt::gpu::MetalContext ctx;
    if (!ctx.valid()) {
        std::fprintf(stderr, "ERROR: Metal device/queue failed to initialise\n");
        pool->release();
        return 1;
    }

    const topopt::gpu::DeviceCaps caps = ctx.caps();
    std::printf("Metal device : %s\n", caps.name.c_str());
    std::printf("GPU family   : %s\n", caps.gpuFamily.c_str());
    std::printf("Working set  : %.2f GB\n",
                static_cast<double>(caps.recommendedWorkingSetBytes) / 1e9);
    std::printf("Unified mem  : %s\n", caps.unifiedMemory ? "yes" : "no");

    if (!ctx.loadLibrary("build/shaders.metallib")) {
        pool->release();
        return 1;
    }
    MTL::ComputePipelineState* pso = ctx.makePipeline("vec_add");
    if (!pso) {
        pool->release();
        return 1;
    }

    const std::uint32_t n = 1u << 20;  // 1,048,576 elements
    const std::size_t bytes = static_cast<std::size_t>(n) * sizeof(float);
    MTL::Device* dev = ctx.device();
    MTL::Buffer* ba = dev->newBuffer(bytes, MTL::ResourceStorageModeShared);
    MTL::Buffer* bb = dev->newBuffer(bytes, MTL::ResourceStorageModeShared);
    MTL::Buffer* bc = dev->newBuffer(bytes, MTL::ResourceStorageModeShared);

    float* a = static_cast<float*>(ba->contents());
    float* b = static_cast<float*>(bb->contents());
    std::vector<float> cpu(n);
    std::mt19937 rng(12345u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (std::uint32_t i = 0; i < n; ++i) {
        a[i] = dist(rng);
        b[i] = dist(rng);
        cpu[i] = a[i] + b[i];
    }

    MTL::CommandBuffer* cmd = ctx.queue()->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(ba, 0, 0);
    enc->setBuffer(bb, 0, 1);
    enc->setBuffer(bc, 0, 2);
    enc->setBytes(&n, sizeof(n), 3);

    NS::UInteger tg = pso->maxTotalThreadsPerThreadgroup();
    if (tg > 256) tg = 256;  // multiple of the 32-wide SIMD, plenty for vec add
    enc->dispatchThreads(MTL::Size(n, 1, 1), MTL::Size(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();

    const float* c = static_cast<const float*>(bc->contents());
    double maxerr = 0.0;
    for (std::uint32_t i = 0; i < n; ++i)
        maxerr = std::max(maxerr, static_cast<double>(std::fabs(c[i] - cpu[i])));

    const double tol = 1e-6;
    std::printf("vec_add n=%u : max|gpu-cpu| = %.3e\n", n, maxerr);
    const bool ok = maxerr < tol;
    std::printf("%s\n", ok ? "GPU sum matches CPU sum within tolerance 1e-6"
                           : "MISMATCH: GPU result exceeds tolerance 1e-6");

    bc->release();
    bb->release();
    ba->release();
    pso->release();
    pool->release();
    return ok ? 0 : 1;
}
