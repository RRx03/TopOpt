# CLAUDE.md — TopOpt Phase 5 (overrides)

> **Autorité : `../orchestration/MASTER_CLAUDE.md`.** Spécificités Phase 5
> uniquement. Brief : `../orchestration/prompts/PHASE_5_BRIEF.md`. Handoff :
> `../orchestration/handoffs/PHASE_4_TO_5.md`.

## Statut
**Phase 5 démarrée (NIVEAU TRÈS CRITIQUE)** : Stokes + CHT, adjoint triple-couplé.
Base = Phase 4 (thermo-élastique + stress + MMA + adjoints validés DF). Objectif :
TO multiphysique fluide-structure-thermique → chemise de refroidissement de tuyère.

## Décisions d'architecture (avant tout code — cf. docs/DECISIONS.md)
- **Éléments Stokes : Q1-Q1 stabilisé PSPG** (pas Taylor-Hood). Cohérence grille
  structurée / trilinéaire / matrix-free. Q1-Q1 viole inf-sup → PSPG obligatoire
  (LL-LIT-002). ADR-017.
- **Non-dimensionnalisation** : échelles de référence définies AVANT le premier
  assemblage (LL-LIT-012).

## Spécificités Phase 5
- Hérite tout Phase 4 (adjoints DF-validés, MMA, stress, filtre).
- **À venir** : Stokes+Brinkman, solveur saddle-point (indéfini → MINRES/Uzawa, ou
  direct CPU pour les oracles), CHT, **adjoint 3 blocs validé DF (1e-3)** — gate le
  plus dur du projet, filtre Heaviside.
- Discipline inchangée : oracle CPU double précision, validation DF avant tout run.

## Read first
1. `../orchestration/MASTER_CLAUDE.md`
2. Ce fichier · `STATUS.md`
3. `../orchestration/handoffs/PHASE_4_TO_5.md` · `../orchestration/prompts/PHASE_5_BRIEF.md`

## Read on demand
- `../TopOptP4/PHASE_4_REPORT.md` — adjoints/MMA validés à généraliser
- `docs/DECISIONS.md`, `docs/SYMBOLS.md`
