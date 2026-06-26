# CLAUDE.md — TopOpt Phase 2 (overrides)

> **Autorité : `../orchestration/MASTER_CLAUDE.md`.** Ce fichier ne contient que
> les spécificités Phase 2. Version originale archivée :
> `../analysis/_legacy/TopOptP2/CLAUDE.original.md`.

## Statut
**Phase 2 substantiellement complète (clôturée 2026-06-26).** Solveur TO 3D
**matrix-free** sur GPU Metal, validé (patch test, cantilever, CG vs CPU, MBB 3D),
`make` 0 warning, `make test` vert. Réserve : opti 128³ complète = 16.6 min (cible
< 10 min non tenue avec Jacobi → corrigé par le multigrid Phase 3). Rapport :
`PHASE_2_REPORT.md`. Handoff : `../orchestration/handoffs/PHASE_2_TO_3.md`.

## Spécificités Phase 2 (font foi — cf. `PHASE_2_REPORT.md`)
- C++23 + **metal-cpp non-ARC** (ref counting manuel, `*_PRIVATE_IMPLEMENTATION`
  dans `src/gpu/metal_impl.cpp`, forward decls MTL:: dans les headers).
- Build **two-phase** : `.metal → build/shaders.metallib`, puis C++ + link.
  Makefile partitionné CPU / GPU_CORE / GPU_SOLVER / IO.
- **Matrix-free** : K jamais assemblée ; K·u par node-gather (kernel `mf_matvec_elastic`).
  CG Jacobi (`CGSolver3D`). Filtre Helmholtz matrix-free scalaire (`Helmholtz3D`).
- **Emin = 1e-4** (pas 1e-9) : requis par le CG itératif float32 (LL-006).
- `newLibrary("build/shaders.metallib")`, `StorageModeShared`. Deps via symlinks
  `third_party/{metal-cpp,eigen,nlohmann}` → `../shared/third_party`.
- ADR-001..008 : `docs/DECISIONS.md`.
- Commands : `make`, `make test`, `make run` (mbb 60³), `./build/topopt bench 128`.

## Commands
- Build : `make` · Test : `make test` (vec_add GPU vs CPU) · Run : `make run` · Clean : `make clean`

## Read first
1. `../orchestration/MASTER_CLAUDE.md`
2. Ce fichier · `STATUS.md` · `PHASE_2_REPORT.md`
3. `../orchestration/handoffs/PHASE_1_TO_2.md`

## Read on demand
- `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, `docs/SYMBOLS.md`
- `../analysis/CODE_ANALYSIS.md` — faits sur le code réel
