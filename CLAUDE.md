# CLAUDE.md — TopOpt Phase 3 (overrides)

> **Autorité : `../orchestration/MASTER_CLAUDE.md`.** Spécificités Phase 3
> uniquement. Brief : `../orchestration/prompts/PHASE_3_BRIEF.md`. Handoff :
> `../orchestration/handoffs/PHASE_2_TO_3.md`.

## Statut
**Phase 3 en cours — session 1 faite.** Base = solveur matrix-free Phase 2 (copié).
Objectif : multi-grid uniforme + mesh independence + filtre rayon physique (mm).

Session 1 livrée : `Grid3DMultiLevel` (hiérarchie ×2), `GridTransfer`
(prolongation/restriction densité, **conservatives**), `HelmholtzFilterPhysical`
(rayon mm). Tests : round-trip 2.2e-16, conservation volume exacte. 0 warning.

## Spécificités Phase 3
- Hérite de tout Phase 2 (matrix-free CG/Jacobi, Helmholtz3D, SIMP3D, STL).
- **Nouveau** : hiérarchie de grilles 32³→…→256³ (facteur 2), warm-start,
  filtre Helmholtz **rayon en mm** (mesh-independent, LL-LIT-006).
- **Priorité perf** : préconditionneur **multigrid V-cycle** (faire tomber le coût
  CG : 1516→4001 iter à 128³ en fin d'opti). Viser opti 128³ < 10 min.
- Conservation de volume inter-niveaux obligatoire (LL-LIT-010) — déjà testée.
- Décision à acter : continuation de p entre niveaux → `docs/DECISIONS.md`.

## Commands
- Build : `make` · Tests : `make test` (+ `make test_cpu` pour multigrid/fem sans GPU)
- Run : `make run` · Clean : `make clean`

## Read first
1. `../orchestration/MASTER_CLAUDE.md`
2. Ce fichier · `STATUS.md`
3. `../orchestration/handoffs/PHASE_2_TO_3.md` · `../orchestration/prompts/PHASE_3_BRIEF.md`

## Read on demand
- `../TopOptP2/PHASE_2_REPORT.md` — base matrix-free
- `docs/DECISIONS.md`, `docs/SYMBOLS.md`
