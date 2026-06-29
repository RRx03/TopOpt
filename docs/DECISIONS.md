# Decisions — TopOptP5 (Phase 5)

(ADR-001..016 hérités des phases précédentes — voir ../TopOptP4/docs/DECISIONS.md.)

## ADR-017 : Éléments Stokes Q1-Q1 stabilisés PSPG (pas Taylor-Hood)
- **Date** : 2026-06-29
- **Contexte** : Stokes incompressible exige des éléments inf-sup stables
  (Babuška-Brezzi, LL-LIT-002), sinon oscillations de pression / design absurde.
- **Options** : Taylor-Hood (P2 vitesse, P1 pression, stable par construction) ;
  Q1-Q1 égal-ordre + stabilisation PSPG.
- **Décision** : **Q1-Q1 + PSPG**. L'ADN du projet est grille structurée +
  trilinéaire (H8/Q4) + matrix-free/GPU ; Q1-Q1 partage exactement la grille et les
  fonctions de forme (u et p aux mêmes nœuds). Taylor-Hood P2 imposerait des nœuds
  milieux d'arête, cassant l'uniformité structurée et la machinerie existante.
- **Conséquences** : Q1-Q1 viole inf-sup → terme de stabilisation PSPG (résidu de
  moment projeté sur ∇q, paramètre τ ∼ h²/μ) OBLIGATOIRE. À valider sur Poiseuille
  (pas d'oscillation de pression). Le paramètre τ devra être documenté/calibré.

## ADR-018 : Oracles Stokes en CPU double précision (Eigen direct)
- **Date** : 2026-06-29
- **Décision** : comme pour les adjoints P4, valider le solveur Stokes et l'adjoint
  fluide en CPU double (système saddle-point résolu en direct Eigen), pas GPU float32.
- **Conséquences** : oracle propre (Poiseuille, DF adjoint) ; portage GPU
  (MINRES/Uzawa) en production, re-validé contre le chemin CPU.
