# Status — 2026-06-15

## Current focus
Phase 2, session 1 : fondation Metal GPU — **terminée et validée**.

## Last session (2026-06-15)
- TopOptP2 créé vierge (Metal-only), frère de TopOptP1.
- metal-cpp officiel (macOS26) vendorisé dans third_party/metal-cpp.
- Makefile two-phase (shaders→metallib, C++ + frameworks), **0 warning**.
- MetalContext (device/queue/library/caps) + metal_impl (TU unique).
- shaders/vector_add.metal + tests/test_metal_hello (vec add 1 M).
- Validé : Apple M4 Max, family Apple9, 55.66 GB ; GPU sum == CPU (err 0) < 1e-6.
- Commit : WIP (non commité)

## Next up
1. Élément hexaédrique H8 + assembly FEM 3D (CPU d'abord, référence).
2. Portage 2D→3D des structures (Grid, SIMP, OC, Helmholtz) depuis Phase 1.
3. Assembly + SpMV en kernels Metal ; CG préconditionné Jacobi sur GPU.

## Blockers
- Aucun.

## WIP files
- Aucun (livrable de session complet).

## Don't touch
- `../TopOptP1` (Phase 1 figée, trace).
- `third_party/metal-cpp` (vendorisé, ne pas éditer).
