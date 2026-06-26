# Status — 2026-06-26

## Current focus
**Phase 3, session 1 faite** : hiérarchie de grilles + opérateurs inter-grilles.

## Last session (2026-06-26)
- Squelette TopOptP3 copié de la base matrix-free Phase 2 (build 0 warning).
- `Grid3DMultiLevel` : hiérarchie ×2, cell size physique par niveau.
- `GridTransfer` : prolongation (injection) / restriction (moyenne) du champ
  densité, **conservatives** (round-trip 2.2e-16, volume exact).
- `HelmholtzFilterPhysical` : filtre Helmholtz rayon en mm (= r_mm/h).
- `test_multigrid` (CPU) : ALL PASS.

## Next up
1. **Préconditionneur multigrid V-cycle** (prolongation/restriction NODALES GPU +
   smoother red-black Gauss-Seidel) — priorité perf, viser opti 128³ < 10 min.
2. `MultiGridOptimizer` : loop multi-grid + warm-start (décision continuation de p).
3. Démonstration **mesh independence** : MBB 3D 64³ vs 128³ vs 256³, r mm fixe.
4. Marching cubes lisse (remplace surface voxels).

## Blockers
- Aucun.

## Don't touch
- `../TopOptP1`, `../TopOptP2` (figés).
- `third_party/*` (symlinks vers ../shared/third_party).
