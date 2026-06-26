# CLAUDE.md — TopOpt Phase 2 (overrides)

> **Autorité : `../orchestration/MASTER_CLAUDE.md`.** Ce fichier ne contient que
> les spécificités Phase 2. Version originale archivée :
> `../analysis/_legacy/TopOptP2/CLAUDE.original.md`.

## Statut
**Phase 2 incomplète : ~1/9 (fondation Metal seule).** Le solveur FEM 3D GPU
(H8, assembly CSR, SpMV, CG, STL) **reste à écrire**. Rapport honnête :
`PHASE_2_REPORT.md`. **Ne pas démarrer Phase 3 avant d'avoir terminé Phase 2**
(cf. `../orchestration/handoffs/PHASE_2_TO_3.md`).

## Spécificités Phase 2 (font foi — cf. `../analysis/CODE_ANALYSIS.md`)
- C++23 + **metal-cpp non-ARC** (ref counting manuel, `*_PRIVATE_IMPLEMENTATION`
  dans `src/gpu/metal_impl.cpp`, forward decls MTL:: dans les headers).
- Build **two-phase** : `.metal → build/shaders.metallib`, puis C++ + link
  (`-framework Metal -framework Foundation -framework QuartzCore`).
- `newLibrary("build/shaders.metallib")` (CLI, pas `newDefaultLibrary`),
  `StorageModeShared` (mémoire unifiée).
- Vendoring : `third_party/metal-cpp` → symlink vers `../shared/third_party/metal-cpp`.
- ADR-001..004 (vierge Metal-only, float démo, build 0 warning) : `docs/DECISIONS.md`.
- Reste-à-faire : H8, assembly CSR GPU (atomicAdd, LL-LIT-008), SpMV, CG Jacobi
  (float, LL-LIT-009), filtre GPU, marching cubes → STL.

## Commands
- Build : `make` · Test : `make test` (vec_add GPU vs CPU) · Run : `make run` · Clean : `make clean`

## Read first
1. `../orchestration/MASTER_CLAUDE.md`
2. Ce fichier · `STATUS.md` · `PHASE_2_REPORT.md`
3. `../orchestration/handoffs/PHASE_1_TO_2.md`

## Read on demand
- `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, `docs/SYMBOLS.md`
- `../analysis/CODE_ANALYSIS.md` — faits sur le code réel
