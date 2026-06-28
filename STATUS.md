# Status — 2026-06-28

## Current focus
**Phase 4 (NIVEAU CRITIQUE)** : étapes 1, 2 et le GATE adjoint faits.

## Progression Phase 4 (ordre du brief)
1. ✅ Solveur thermique seul (`ThermalSolver`, GPU) — plaque gradient 8.9e-7.
2. ✅ Couplage thermo-élastique faible (`ThermoElasticCoupling`) — dilatation libre 7.4e-6.
4. ✅ **GATE adjoint 2 blocs validé par DF** (`ThermoElasticAdjoint`) — **1.6e-6 < 1e-5**.
   - Validation CPU double précision (Eigen direct), DF centrées 8³, 20 éléments.
   - Gradient = term_elastic − term_thload + term_cond ; les 3 actifs (couplage exercé).
   - Réalisé par sous-session Agent, vérifié indépendamment (rebuild + re-run).

## Next up
3. von Mises + p-norm + **ε-relaxation** (LL-LIT-001) — étend J ; l'adjoint est en place.
5. **MMA** (remplace OC) — multi-contraintes.
6. Géométrie 2D axisymétrique (singularité r=0).
7. Cas tuyère 2D axi (pression + flux thermique) ; vérif épaississement au col.

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
