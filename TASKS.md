# Tasks — TopOptP2 (Phase 2)

## Active
- (Phase 2 clôturée — voir Phase 3 dans ../orchestration/prompts/PHASE_3_BRIEF.md)

## Done (last 14 days)
- [x] 2026-06-15 : fondation Metal — MetalContext, vendoring metal-cpp, hello-world
- [x] 2026-06-26 : Grid3D + H8Element (KE0 24×24, Le/Me) + FEM3D référence CPU
- [x] 2026-06-26 : CG matrix-free GPU + Jacobi (CGSolver3D) — validé vs CPU 3.1e-4
- [x] 2026-06-26 : filtre Helmholtz GPU matrix-free (Helmholtz3D)
- [x] 2026-06-26 : SIMP3D + OC (volume-preserving) ; loop TO 3D (main)
- [x] 2026-06-26 : STLExporter (surface voxels) ; MBB 3D + STL
- [x] 2026-06-26 : tests (patch/cantilever/CG/MBB) ; benchmarks 64³/128³
- [x] 2026-06-26 : précision tranchée — float32 GPU (CG converge), Emin=1e-4

## Backlog (Phase 3)
- [ ] Multigrid : prolongation/restriction conservatives + warm-start
- [ ] Préconditionneur multigrid V-cycle (opti 128³ < 10 min)
- [ ] Filtre rayon physique (mm) — mesh independence
- [ ] Marching cubes lisse (remplace surface voxels)
- [ ] Batching command-buffers (réduire les sync CPU↔GPU par opération)

## Git log (recent)
- voir `git log` (P2) — baseline + clôture Phase 2
