#include <metal_stdlib>
using namespace metal;

// Trivial element-wise add: c = a + b. Bounds-checked for non-uniform grids.
kernel void vec_add(device const float* a [[buffer(0)]],
                    device const float* b [[buffer(1)]],
                    device float*       c [[buffer(2)]],
                    constant uint&      n [[buffer(3)]],
                    uint tid [[thread_position_in_grid]]) {
    if (tid >= n) return;
    c[tid] = a[tid] + b[tid];
}
