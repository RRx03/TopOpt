#pragma once

#include <cstdint>
#include <string>

// Forward declarations — keep the heavy metal-cpp headers out of this header.
namespace MTL {
class Device;
class CommandQueue;
class Library;
class ComputePipelineState;
} // namespace MTL

namespace topopt::gpu {

struct DeviceCaps {
    std::string name;
    std::string gpuFamily;                 // highest supported Apple GPU family
    std::uint64_t recommendedWorkingSetBytes = 0;
    bool unifiedMemory = false;
};

// Owns the Metal device, command queue, and (optionally) a loaded shader
// library. Manual ref counting (metal-cpp is not ARC) — released in dtor.
class MetalContext {
public:
    MetalContext();
    ~MetalContext();
    MetalContext(const MetalContext&) = delete;
    MetalContext& operator=(const MetalContext&) = delete;

    bool valid() const { return device_ != nullptr && queue_ != nullptr; }
    DeviceCaps caps() const;

    MTL::Device* device() const { return device_; }
    MTL::CommandQueue* queue() const { return queue_; }

    // Load a compiled .metallib from disk. Returns false (and logs) on failure.
    bool loadLibrary(const std::string& path);

    // Build a compute pipeline for a kernel in the loaded library.
    // Caller owns the returned pipeline and must release() it. nullptr on error.
    MTL::ComputePipelineState* makePipeline(const std::string& functionName);

private:
    MTL::Device* device_ = nullptr;
    MTL::CommandQueue* queue_ = nullptr;
    MTL::Library* library_ = nullptr;
};

} // namespace topopt::gpu
