# Status — 2026-06-26

## Current focus
**Phase 2 clôturée** : solveur TO 3D matrix-free sur GPU Metal, validé.

## Last session (2026-06-26)
- Fondation 3D : Grid3D, H8Element (KE0 24×24, Le/Me), FEM3D (référence CPU LDLT).
- GPU matrix-free : CGSolver3D (CG Jacobi élasticité), Helmholtz3D (filtre scalaire),
  kernels `shaders/fem3d.metal`. SIMP3D (OC volume-preserving). STLExporter.
- Tests : patch 3D 7.8e-16, cantilever 0.977, CG vs CPU 3.1e-4, MBB 3D OK. 0 warning.
- Benchmarks : solve 64³ 0.57 s ; **solve 128³ 6.4 s (1516 iter)** ; opti 128³
  60-iter 16.6 min (compliance 6.39→0.327, vol 0.3000), STL 263k tris.
- Décisions : matrix-free (ADR-005), Emin=1e-4 (ADR-006, LL-006), filtre conservatif
  (ADR-007, LL-007), STL voxels (ADR-008).

## Next up (Phase 3)
1. Multigrid (prolongation/restriction, warm-start) — faire tomber le coût CG.
2. Filtre rayon physique (mm) → mesh independence.
3. Préconditionneur multigrid pour viser opti 128³ < 10 min.
→ `../orchestration/handoffs/PHASE_2_TO_3.md`, `../orchestration/prompts/PHASE_3_BRIEF.md`.

## Blockers
- Aucun. Réserve : opti 128³ = 16.6 min (cible <10 min) → Jacobi faible, fix Phase 3.

## WIP files
- Aucun (Phase 2 livrée, tests verts).

## Don't touch
- `../TopOptP1` (Phase 1 figée).
- `third_party/*` (symlinks vers ../shared/third_party).
