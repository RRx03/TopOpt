#include <metal_stdlib>
using namespace metal;

// Matrix-free steady-state heat conduction on the structured H8 grid:
//   -div(k(rho) grad T) = q.
// Scalar field (1 DOF per node). Per-element conductivity k_e scales the shared
// 8x8 element Laplacian L0 (= H8Element::diffusion()). K*T recomputed per node,
// never assembled — mirrors the elastic matrix-free kernels.

struct GridDims {
    uint nelx, nely, nelz, nnodes;
};

inline float kcond_at(device const float* Kc, GridDims g, int ex, int ey, int ez) {
    return Kc[uint(ex) + uint(ey) * g.nelx + uint(ez) * g.nelx * g.nely];
}

// y = K_thermal * T, one thread per node.
kernel void mf_matvec_thermal(device const float* T  [[buffer(0)]],
                              device const float* Kc  [[buffer(1)]],
                              constant float* L0       [[buffer(2)]],
                              constant GridDims& g     [[buffer(3)]],
                              device float* Y          [[buffer(4)]],
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
                const float ke = kcond_at(Kc, g, ex, ey, ez);
                float te[8];
                for (int b = 0; b < 8; ++b) {
                    const int bi = b & 1, bj = (b >> 1) & 1, bk = (b >> 2) & 1;
                    te[b] = T[uint(ex + bi) + uint(ey + bj) * nx
                             + uint(ez + bk) * nx * ny];
                }
                float s = 0.0;
                for (int m = 0; m < 8; ++m) s += L0[l * 8 + m] * te[m];
                acc += ke * s;
            }
        }
    }
    Y[tid] = acc;
}

// Diagonal of K_thermal (Jacobi), one thread per node.
kernel void mf_diag_thermal(device const float* Kc [[buffer(0)]],
                            constant float* L0      [[buffer(1)]],
                            constant GridDims& g    [[buffer(2)]],
                            device float* D         [[buffer(3)]],
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
                d += kcond_at(Kc, g, ex, ey, ez) * L0[l * 8 + l];
            }
        }
    }
    D[tid] = d;
}
