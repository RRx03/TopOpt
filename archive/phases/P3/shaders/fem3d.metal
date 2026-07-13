#include <metal_stdlib>
using namespace metal;

// Structured H8 grid, matrix-free linear elasticity. Node numbering:
//   node(i,j,k) = i + j*nx + k*nx*ny,  nx=nelx+1, etc.  DOF: 3*node + {0,1,2}.
// Element local node l = di + 2*dj + 4*dk; KE0 is the unit-modulus 24x24
// stiffness (row-major). K*u is recomputed on the fly (never assembled).

struct GridDims {
    uint nelx, nely, nelz, nnodes;
};

// Gather element modulus for element (ex,ey,ez).
inline float emod_at(device const float* Emod, GridDims g,
                     int ex, int ey, int ez) {
    return Emod[uint(ex) + uint(ey) * g.nelx + uint(ez) * g.nelx * g.nely];
}

// --- matrix-free K*u, one thread per node ---------------------------------
kernel void mf_matvec_elastic(device const float* U     [[buffer(0)]],
                              device const float* Emod   [[buffer(1)]],
                              constant float* KE0        [[buffer(2)]],
                              constant GridDims& g       [[buffer(3)]],
                              device float* Y            [[buffer(4)]],
                              uint tid [[thread_position_in_grid]]) {
    if (tid >= g.nnodes) return;
    const uint nx = g.nelx + 1, ny = g.nely + 1;
    const int i = int(tid % nx);
    const uint t = tid / nx;
    const int j = int(t % ny);
    const int k = int(t / ny);

    float3 acc = float3(0.0);

    for (int ax = 0; ax < 2; ++ax) {
        const int ex = i - 1 + ax;
        if (ex < 0 || ex >= int(g.nelx)) continue;
        const int di = i - ex;                       // 0 or 1
        for (int ay = 0; ay < 2; ++ay) {
            const int ey = j - 1 + ay;
            if (ey < 0 || ey >= int(g.nely)) continue;
            const int dj = j - ey;
            for (int az = 0; az < 2; ++az) {
                const int ez = k - 1 + az;
                if (ez < 0 || ez >= int(g.nelz)) continue;
                const int dk = k - ez;
                const int l = di + 2 * dj + 4 * dk;   // local index of this node

                const float Ee = emod_at(Emod, g, ex, ey, ez);

                // Gather 24 local displacements.
                float ue[24];
                for (int b = 0; b < 8; ++b) {
                    const int bi = b & 1, bj = (b >> 1) & 1, bk = (b >> 2) & 1;
                    const uint nb = uint(ex + bi) + uint(ey + bj) * nx
                                  + uint(ez + bk) * nx * ny;
                    ue[3 * b + 0] = U[3 * nb + 0];
                    ue[3 * b + 1] = U[3 * nb + 1];
                    ue[3 * b + 2] = U[3 * nb + 2];
                }
                // Three rows (3l..3l+2) of Ee*KE0 times ue.
                for (int c = 0; c < 3; ++c) {
                    const int row = 3 * l + c;
                    float s = 0.0;
                    for (int m = 0; m < 24; ++m)
                        s += KE0[row * 24 + m] * ue[m];
                    acc[c] += Ee * s;
                }
            }
        }
    }
    Y[3 * tid + 0] = acc.x;
    Y[3 * tid + 1] = acc.y;
    Y[3 * tid + 2] = acc.z;
}

// --- diagonal of K (Jacobi preconditioner), one thread per node -----------
kernel void mf_diag_elastic(device const float* Emod [[buffer(0)]],
                            constant float* KE0      [[buffer(1)]],
                            constant GridDims& g     [[buffer(2)]],
                            device float* D          [[buffer(3)]],
                            uint tid [[thread_position_in_grid]]) {
    if (tid >= g.nnodes) return;
    const uint nx = g.nelx + 1, ny = g.nely + 1;
    const int i = int(tid % nx);
    const uint t = tid / nx;
    const int j = int(t % ny);
    const int k = int(t / ny);

    float3 d = float3(0.0);
    for (int ax = 0; ax < 2; ++ax) {
        const int ex = i - 1 + ax;
        if (ex < 0 || ex >= int(g.nelx)) continue;
        const int di = i - ex;
        for (int ay = 0; ay < 2; ++ay) {
            const int ey = j - 1 + ay;
            if (ey < 0 || ey >= int(g.nely)) continue;
            const int dj = j - ey;
            for (int az = 0; az < 2; ++az) {
                const int ez = k - 1 + az;
                if (ez < 0 || ez >= int(g.nelz)) continue;
                const int dk = k - ez;
                const int l = di + 2 * dj + 4 * dk;
                const float Ee = emod_at(Emod, g, ex, ey, ez);
                for (int c = 0; c < 3; ++c) {
                    const int row = 3 * l + c;
                    d[c] += Ee * KE0[row * 24 + row];
                }
            }
        }
    }
    D[3 * tid + 0] = d.x;
    D[3 * tid + 1] = d.y;
    D[3 * tid + 2] = d.z;
}

// --- BC and vector kernels (1 thread per DOF) -----------------------------
kernel void zero_fixed(device float* x          [[buffer(0)]],
                       device const uchar* fixed [[buffer(1)]],
                       constant uint& n          [[buffer(2)]],
                       uint tid [[thread_position_in_grid]]) {
    if (tid >= n) return;
    if (fixed[tid]) x[tid] = 0.0;
}

// z = r * invdiag, but keep fixed DOFs null.
kernel void precond_jacobi(device const float* r       [[buffer(0)]],
                           device const float* invdiag [[buffer(1)]],
                           device const uchar* fixed    [[buffer(2)]],
                           device float* z              [[buffer(3)]],
                           constant uint& n             [[buffer(4)]],
                           uint tid [[thread_position_in_grid]]) {
    if (tid >= n) return;
    z[tid] = fixed[tid] ? 0.0 : r[tid] * invdiag[tid];
}

// y = y + a*x
kernel void vec_axpy(device float* y       [[buffer(0)]],
                     device const float* x [[buffer(1)]],
                     constant float& a     [[buffer(2)]],
                     constant uint& n      [[buffer(3)]],
                     uint tid [[thread_position_in_grid]]) {
    if (tid >= n) return;
    y[tid] += a * x[tid];
}

// y = x + b*y
kernel void vec_xpby(device float* y       [[buffer(0)]],
                     device const float* x [[buffer(1)]],
                     constant float& b     [[buffer(2)]],
                     constant uint& n      [[buffer(3)]],
                     uint tid [[thread_position_in_grid]]) {
    if (tid >= n) return;
    y[tid] = x[tid] + b * y[tid];
}

kernel void vec_copy(device float* dst       [[buffer(0)]],
                     device const float* src [[buffer(1)]],
                     constant uint& n        [[buffer(2)]],
                     uint tid [[thread_position_in_grid]]) {
    if (tid >= n) return;
    dst[tid] = src[tid];
}

// Partial dot product: one partial per threadgroup (reduced on CPU).
kernel void vec_dot_partial(device const float* a [[buffer(0)]],
                            device const float* b [[buffer(1)]],
                            constant uint& n      [[buffer(2)]],
                            device float* partials [[buffer(3)]],
                            uint tid  [[thread_position_in_grid]],
                            uint lid  [[thread_position_in_threadgroup]],
                            uint tgid [[threadgroup_position_in_grid]],
                            uint tgsz [[threads_per_threadgroup]]) {
    threadgroup float scratch[256];
    float v = (tid < n) ? a[tid] * b[tid] : 0.0;
    scratch[lid] = v;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint s = tgsz / 2; s > 0; s >>= 1) {
        if (lid < s) scratch[lid] += scratch[lid + s];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (lid == 0) partials[tgid] = scratch[0];
}

// =========================================================================
// Helmholtz density filter (scalar, 1 value per node). Operator KF is the
// same for every element (design-independent): KF0 = rlen^2*Le + Me (8x8).
// =========================================================================

// Scatter element field -> node RHS: rhs_n = sum_e (incident) x_e * (1/8).
kernel void helm_scatter(device const float* xe [[buffer(0)]],
                         constant GridDims& g   [[buffer(1)]],
                         device float* rhs       [[buffer(2)]],
                         uint tid [[thread_position_in_grid]]) {
    if (tid >= g.nnodes) return;
    const uint nx = g.nelx + 1, ny = g.nely + 1;
    const int i = int(tid % nx);
    const uint t = tid / nx;
    const int j = int(t % ny);
    const int k = int(t / ny);
    float s = 0.0;
    for (int ax = 0; ax < 2; ++ax) {
        const int ex = i - 1 + ax; if (ex < 0 || ex >= int(g.nelx)) continue;
        for (int ay = 0; ay < 2; ++ay) {
            const int ey = j - 1 + ay; if (ey < 0 || ey >= int(g.nely)) continue;
            for (int az = 0; az < 2; ++az) {
                const int ez = k - 1 + az; if (ez < 0 || ez >= int(g.nelz)) continue;
                s += xe[uint(ex) + uint(ey) * g.nelx + uint(ez) * g.nelx * g.nely];
            }
        }
    }
    rhs[tid] = s * 0.125;
}

// Gather node field -> element field: out_e = mean of its 8 nodal values.
kernel void helm_gather(device const float* yn [[buffer(0)]],
                        constant GridDims& g    [[buffer(1)]],
                        device float* oe         [[buffer(2)]],
                        uint tid [[thread_position_in_grid]]) {
    const uint nelems = g.nelx * g.nely * g.nelz;
    if (tid >= nelems) return;
    const uint nx = g.nelx + 1, ny = g.nely + 1;
    const uint ex = tid % g.nelx;
    const uint t = tid / g.nelx;
    const uint ey = t % g.nely;
    const uint ez = t / g.nely;
    float s = 0.0;
    for (int b = 0; b < 8; ++b) {
        const uint bi = b & 1, bj = (b >> 1) & 1, bk = (b >> 2) & 1;
        s += yn[(ex + bi) + (ey + bj) * nx + (ez + bk) * nx * ny];
    }
    oe[tid] = s * 0.125;
}

// Matrix-free KF*y (scalar), one thread per node. KF0 is the 8x8 element matrix.
kernel void mf_matvec_helmholtz(device const float* Y [[buffer(0)]],
                                constant float* KF0    [[buffer(1)]],
                                constant GridDims& g   [[buffer(2)]],
                                device float* out       [[buffer(3)]],
                                uint tid [[thread_position_in_grid]]) {
    if (tid >= g.nnodes) return;
    const uint nx = g.nelx + 1, ny = g.nely + 1;
    const int i = int(tid % nx);
    const uint t = tid / nx;
    const int j = int(t % ny);
    const int k = int(t / ny);
    float acc = 0.0;
    for (int ax = 0; ax < 2; ++ax) {
        const int ex = i - 1 + ax; if (ex < 0 || ex >= int(g.nelx)) continue;
        const int di = i - ex;
        for (int ay = 0; ay < 2; ++ay) {
            const int ey = j - 1 + ay; if (ey < 0 || ey >= int(g.nely)) continue;
            const int dj = j - ey;
            for (int az = 0; az < 2; ++az) {
                const int ez = k - 1 + az; if (ez < 0 || ez >= int(g.nelz)) continue;
                const int dk = k - ez;
                const int l = di + 2 * dj + 4 * dk;
                float yev[8];
                for (int b = 0; b < 8; ++b) {
                    const int bi = b & 1, bj = (b >> 1) & 1, bk = (b >> 2) & 1;
                    yev[b] = Y[uint(ex + bi) + uint(ey + bj) * nx
                              + uint(ez + bk) * nx * ny];
                }
                float s = 0.0;
                for (int m = 0; m < 8; ++m) s += KF0[l * 8 + m] * yev[m];
                acc += s;
            }
        }
    }
    out[tid] = acc;
}

// Diagonal of KF (scalar Jacobi), one thread per node.
kernel void mf_diag_helmholtz(constant float* KF0  [[buffer(0)]],
                              constant GridDims& g  [[buffer(1)]],
                              device float* D        [[buffer(2)]],
                              uint tid [[thread_position_in_grid]]) {
    if (tid >= g.nnodes) return;
    const uint nx = g.nelx + 1, ny = g.nely + 1;
    const int i = int(tid % nx);
    const uint t = tid / nx;
    const int j = int(t % ny);
    const int k = int(t / ny);
    float d = 0.0;
    for (int ax = 0; ax < 2; ++ax) {
        const int ex = i - 1 + ax; if (ex < 0 || ex >= int(g.nelx)) continue;
        const int di = i - ex;
        for (int ay = 0; ay < 2; ++ay) {
            const int ey = j - 1 + ay; if (ey < 0 || ey >= int(g.nely)) continue;
            const int dj = j - ey;
            for (int az = 0; az < 2; ++az) {
                const int ez = k - 1 + az; if (ez < 0 || ez >= int(g.nelz)) continue;
                const int dk = k - ez;
                const int l = di + 2 * dj + 4 * dk;
                d += KF0[l * 8 + l];
            }
        }
    }
    D[tid] = d;
}

// --- per-element unit-modulus strain energy ce = u_e^T KE0 u_e ------------
kernel void mf_strain_energy(device const float* U  [[buffer(0)]],
                             constant float* KE0     [[buffer(1)]],
                             constant GridDims& g    [[buffer(2)]],
                             device float* ce         [[buffer(3)]],
                             uint tid [[thread_position_in_grid]]) {
    const uint nelems = g.nelx * g.nely * g.nelz;
    if (tid >= nelems) return;
    const uint nx = g.nelx + 1, ny = g.nely + 1;
    const uint ex = tid % g.nelx;
    const uint t = tid / g.nelx;
    const uint ey = t % g.nely;
    const uint ez = t / g.nely;
    float ue[24];
    for (int b = 0; b < 8; ++b) {
        const uint bi = b & 1, bj = (b >> 1) & 1, bk = (b >> 2) & 1;
        const uint nb = (ex + bi) + (ey + bj) * nx + (ez + bk) * nx * ny;
        ue[3 * b + 0] = U[3 * nb + 0];
        ue[3 * b + 1] = U[3 * nb + 1];
        ue[3 * b + 2] = U[3 * nb + 2];
    }
    float s = 0.0;
    for (int a = 0; a < 24; ++a) {
        float row = 0.0;
        for (int m = 0; m < 24; ++m) row += KE0[a * 24 + m] * ue[m];
        s += ue[a] * row;
    }
    ce[tid] = s;
}
