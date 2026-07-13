# CLAUDE.md — TopOpt Phase 1 (overrides)

> **Autorité : `../orchestration/MASTER_CLAUDE.md`.** Ce fichier ne contient que
> les spécificités Phase 1. Version originale archivée :
> `../analysis/_legacy/TopOptP1/CLAUDE.original.md`.

## Statut
**Phase 1 terminée et validée** (`make test` → ALL PASS). TO structurelle 2D SIMP.
Rapport : `PHASE_1_REPORT.md`. Ne pas modifier le code (phase archivée).

## Spécificités Phase 1 (font foi — cf. `../analysis/CODE_ANALYSIS.md`)
- Élasticité **Q4 plane stress**, KE0 analytique top88, numérotation **column-major**
  `node_id = col*(nely+1)+row` (row 0 = haut).
- Solveur **direct `Eigen::SimplicialLDLT`** (pas CG, pas LLT) — choix assumé
  (Emin=1e-9 → conditionnement ; direct robuste < 30 s). Cf. ADR-001.
- SIMP `E = Emin + ρ^p(E0−Emin)`, **Emin=1e-9**, **ρ ∈ [0,1]**, p=3 (pas de continuation).
- OC par **bissection** (move=0.2, exp 0.5). Filtre Helmholtz PDE, **rayon en cellules**
  `r = R/(2√3)`. Cf. ADR-002.
- Modules **fusionnés** : `FEM2D` (élément+assembly+solveur), `SIMP` (objectif+OC),
  I/O JSON inline dans `main`. 1 fichier de test (`tests/test_mbb_beam.cpp`).

## Commands
- Build : `make` · Test : `make test` · Run : `make run` (MBB 200×100) · Clean : `make clean`

## Read first
1. `../orchestration/MASTER_CLAUDE.md`
2. Ce fichier · `PHASE_1_REPORT.md`

## Read on demand
- `TRANSITIONS.md` — cartographie historique des phases (référence)
- `../analysis/CODE_ANALYSIS.md` — faits sur le code réel
