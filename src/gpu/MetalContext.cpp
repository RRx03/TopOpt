#include "gpu/MetalContext.hpp"

#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

namespace topopt::gpu {

namespace {
// Probe the highest supported Apple GPU family. metal-cpp enum values are
// 1000 + N for AppleN; supportsFamily() safely returns false for unknown ones.
std::string detectAppleFamily(MTL::Device* d) {
    for (int n = 12; n >= 1; --n) {
        const auto fam = static_cast<MTL::GPUFamily>(1000 + n);
        if (d->supportsFamily(fam)) return "Apple" + std::to_string(n);
    }
    return "unknown";
}
} // namespace

MetalContext::MetalContext() {
    device_ = MTL::CreateSystemDefaultDevice();  // +1
    if (device_) queue_ = device_->newCommandQueue();  // +1
}

MetalContext::~MetalContext() {
    if (library_) library_->release();
    if (queue_) queue_->release();
    if (device_) device_->release();
}

DeviceCaps MetalContext::caps() const {
    DeviceCaps c;
    if (!device_) return c;
    c.name = device_->name()->utf8String();
    c.gpuFamily = detectAppleFamily(device_);
    c.recommendedWorkingSetBytes = device_->recommendedMaxWorkingSetSize();
    c.unifiedMemory = device_->hasUnifiedMemory();
    return c;
}

bool MetalContext::loadLibrary(const std::string& path) {
    if (!device_) return false;
    NS::String* nsPath = NS::String::string(path.c_str(), NS::UTF8StringEncoding);
    NS::Error* err = nullptr;
    if (library_) {
        library_->release();
        library_ = nullptr;
    }
    library_ = device_->newLibrary(nsPath, &err);  // +1
    if (!library_) {
        std::fprintf(stderr, "loadLibrary(%s) failed: %s\n", path.c_str(),
                     err ? err->localizedDescription()->utf8String() : "unknown");
        return false;
    }
    return true;
}

MTL::ComputePipelineState* MetalContext::makePipeline(const std::string& fn) {
    if (!device_ || !library_) return nullptr;
    NS::String* name = NS::String::string(fn.c_str(), NS::UTF8StringEncoding);
    MTL::Function* func = library_->newFunction(name);  // +1
    if (!func) {
        std::fprintf(stderr, "kernel '%s' not found in library\n", fn.c_str());
        return nullptr;
    }
    NS::Error* err = nullptr;
    MTL::ComputePipelineState* pso = device_->newComputePipelineState(func, &err);
    func->release();
    if (!pso) {
        std::fprintf(stderr, "pipeline for '%s' failed: %s\n", fn.c_str(),
                     err ? err->localizedDescription()->utf8String() : "unknown");
        return nullptr;
    }
    return pso;
}

} // namespace topopt::gpu
