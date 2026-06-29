# Status — 2026-06-28

## Current focus
**Phase 4 (NIVEAU CRITIQUE)** : étapes 1, 2 et le GATE adjoint faits.

## Progression Phase 4 (ordre du brief)
1. ✅ Solveur thermique seul (`ThermalSolver`, GPU) — plaque gradient 8.9e-7.
2. ✅ Couplage thermo-élastique faible (`ThermoElasticCoupling`) — dilatation libre 7.4e-6.
4. ✅ **GATE adjoint compliance 2 blocs validé DF** (`ThermoElasticAdjoint`) — 1.59e-6.
3. ✅ **Stress** : von Mises + qp-relaxation + p-norm (`StressModel`) — forward validé
   (von Mises uniaxial 2.4e-15, relaxation ρ→0). **GATE sensibilité stress p-norm
   validé DF — 1.62e-7** (adjoint étendu, 4 termes actifs). Compliance toujours verte.

5. ✅ **MMA** (`MMAOptimizer`, Svanberg 1987, sous-problème dual) — oracle analytique
   6.4e-14, cross-check OC 0.037 %, chemin m=2 (Newton dual) OK.

## Next up
6. Géométrie 2D axisymétrique (r,z, facteurs 2πr, singularité r=0) — oracle Lamé.
7. Cas tuyère 2D axi (pression + flux thermique) ; masse min sous σ_PN+T_max via MMA
   + gradients adjoints validés ; vérif épaississement au col.

## Dette explicite
- Adjoints (compliance + stress) validés en **CPU double** (oracle). Portage GPU
  matrix-free float32 à faire pour la production grande échelle, à re-valider contre
  le chemin CPU.

## Blockers / vigilance
- Adjoint multi-bloc : **gate franchi** ✓. Tout nouvel objectif (stress) doit être
  re-validé par DF avant run grande taille.
- Stress singularity : ε-relaxation dès la 1ère version des contraintes.
- LL-008 : clamper densité avant pow, borner bissections (déjà appliqué).

## Note d'implémentation
- L'adjoint validé est en **CPU double (Eigen)** — c'est l'oracle propre. Le portage
  GPU de l'adjoint (production grande échelle) reste à faire quand nécessaire ;
  matrix-free + float32, à re-valider contre ce chemin CPU.

## Don't touch
- `../TopOptP1`, `../TopOptP2`, `../TopOptP3` (figés).
- `third_party/*` (symlinks vers ../shared/third_party).
