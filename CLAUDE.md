# CLAUDE.md — TopOpt Phase 4 (overrides)

> **Autorité : `../orchestration/MASTER_CLAUDE.md`.** Spécificités Phase 4
> uniquement. Brief : `../orchestration/prompts/PHASE_4_BRIEF.md`. Handoff :
> `../orchestration/handoffs/PHASE_3_TO_4.md`.

## Statut
**Phase 4 en cours — session 1 faite (NIVEAU CRITIQUE).** Base = solveur
multi-grid Phase 3 (copié). Objectif : couplage thermo-élastique + von Mises
(p-norm + ε-relaxation) + MMA + adjoint 2 blocs (validé DF) + 2D axisymétrique.

Session 1 livrée : **solveur thermique stationnaire seul** (`ThermalSolver`,
matrix-free GPU scalaire, `−div(k∇T)=q`). Validé : plaque gradient linéaire
T(L) à 8.9e-7, linéarité exacte. Pas encore de couplage.

## Spécificités Phase 4
- Hérite tout Phase 3 (multi-grid, matrix-free, filtre mm, continuation policy).
- Thermique = Laplacien H8 scalaire (`H8Element::diffusion()`), même CG/Jacobi.
- **À venir** : couplage thermo-élastique faible (ε_th=α(T−T_ref)→F_thermal),
  von Mises + p-norm + **ε-relaxation** (LL-LIT-001), **MMA** (remplace OC),
  **adjoint 2 blocs validé par DF** (LL-LIT-007, gate bloquant), 2D axi (r=0).
- Brancher l'ε-relaxation dans `ContinuationParams` (struct générique de P3).
- Si couplage one-way → K asymétrique → BiCGStab/GMRES (LL-LIT-011).

## Commands
- Build : `make` · Tests : `make test` (+ `make test_cpu`) · Clean : `make clean`

## Read first
1. `../orchestration/MASTER_CLAUDE.md`
2. Ce fichier · `STATUS.md`
3. `../orchestration/handoffs/PHASE_3_TO_4.md` · `../orchestration/prompts/PHASE_4_BRIEF.md`

## Read on demand
- `../TopOptP3/PHASE_3_REPORT.md` — base multi-grid
- `docs/DECISIONS.md`, `docs/SYMBOLS.md`
