# ROADMAP.md — Vue d'ensemble des phases TopOpt

*Vue d'ensemble courte. Le détail scientifique opérationnel de chaque phase est
dans `orchestration/prompts/PHASE_N_BRIEF.md`. La cartographie historique
complète (fragilités, pièges, transitions) reste `TopOptP1/TRANSITIONS.md`.*

**Règle d'or** : ne jamais démarrer la phase N+1 si les checkpoints de la phase N
ne sont pas tous verts. La dette technique en TO multiphysique est exponentielle.

---

## Tableau synoptique

| Phase | Objectif (1 phrase) | Durée est. | Livrable phare | Statut |
|---|---|---|---|---|
| **1** | TO structurelle 2D : SIMP + OC + filtre + adjoint compliance | 4-6 sem | MBB beam canonique reproduit | ✅ **Terminée** (validée) |
| **2** | Passage 3D + Metal GPU compute pour FEM creux | 6-8 sem | Solveur CG GPU sur 128³ | 🟡 **Fondation faite** (~1/9), cœur à terminer |
| **3** | Multi-grid uniforme + mesh independence | 4-6 sem | Speedup 5-10× + design indépendant du maillage | ⬜ À venir |
| **4** | Thermo-élastique + von Mises + 2D axisymétrique | 8-10 sem | Tuyère 2D axi sous P + flux thermique | ⬜ À venir |
| **5** | Stokes + CHT : TO multiphysique complète | 10-14 sem | Cooling jacket régénératif optimisé | ⬜ À venir |
| **6** | Extension : industrialisation / recherche / verticalisation | à définir | selon option retenue | ⬜ À arbitrer |

*Durées à ~10 h/semaine.*

---

## Phase 1 — TO structurelle 2D *(terminée)*

- **Pourquoi** : poser la fondation conceptuelle (SIMP, sensibilité adjointe,
  OC, filtre) sur le cas le plus standard de la communauté TO.
- **Prérequis** : aucun.
- **Acquis livrés** : élasticité Q4 plane stress, SIMP, OC par bissection, filtre
  Helmholtz PDE, MBB validé (tests verts). *Cf.* `TopOptP1/PHASE_1_REPORT.md`.
- **Débloque** : la même chaîne TO, à porter en 3D/GPU.

## Phase 2 — 3D + Metal GPU *(fondation faite, cœur à terminer)*

- **Pourquoi** : sans GPU, le 3D à 128³ (~6M DOF) est hors de portée en temps
  raisonnable. La TO est inchangée ; le défi est Metal pour le FEM creux.
- **Prérequis** : chaîne TO 2D validée (Phase 1).
- **État réel** : fondation Metal seule (device/queue/library/pipeline + kernel
  démo). **Manquent** : élément H8, assembly CSR GPU, SpMV, CG préconditionné,
  filtre GPU, marching cubes/STL. *Cf.* `TopOptP2/PHASE_2_REPORT.md`.
- **Débloque** : le calcul 3D à grande échelle, base de toute la multiphysique.

## Phase 3 — Multi-grid + mesh independence

- **Pourquoi** : passage de "ça marche" à "ça marche vite et proprement". Le
  multi-grid accélère la convergence ; le filtre à rayon physique garantit
  l'indépendance au maillage — *la* propriété qui distingue un solveur mature.
- **Prérequis** : solveur 3D GPU **complet et validé** (Phase 2 entière).
- **Acquis attendus** : hiérarchie de grilles, prolongation/restriction,
  warm-start, filtre Helmholtz en mm, mesh independence démontrée, speedup 5-10×.
- **Débloque** : des designs fiables et rapides, prérequis de la multiphysique.

## Phase 4 — Thermo-élastique + von Mises + 2D axi

- **Pourquoi** : première physique couplée + premières vraies contraintes
  ingénieur (stress) + géométrie de tuyère. Saut conceptuel : adjoint multi-bloc,
  p-norm, stress singularity, MMA.
- **Prérequis** : multi-grid + mesh independence (Phase 3).
- **Acquis attendus** : solveur thermique, couplage thermo-élastique, von Mises +
  p-norm + ε-relaxation, adjoint 2 blocs validé par DF, MMA, cas tuyère 2D axi.
- **Débloque** : la moitié de la multiphysique cible ; spécialisation propulsion.

## Phase 5 — Stokes + CHT *(livrable phare)*

- **Pourquoi** : l'aboutissement différenciant — fluide de refroidissement couplé
  à la thermique et à la structure, frontière fluide-solide variable (Brinkman).
- **Prérequis** : thermo-élastique + adjoint multi-bloc validé (Phase 4).
- **Acquis attendus** : Stokes + Brinkman, éléments stables (Taylor-Hood/PSPG),
  solveur saddle-point, CHT, adjoint triple-couplé validé par DF, cooling jacket.
- **Débloque** : le démonstrateur recruteur complet.

## Phase 6 — Extension *(à arbitrer en fin de Phase 5)*

Trois directions, choix selon objectif :
- **A — Industrialisation** (embauche) : manufacturing constraints, robust TO, validation expérimentale.
- **B — Recherche** (thèse) : AMR, adjoint adaptatif, Navier-Stokes/RANS.
- **C — Verticalisation** (entrepreneuriat) : moteur complet, pipeline spec→AM→test.

Le brief Phase 6 reste un template d'options jusqu'à la clôture de Phase 5.

---

## Flux de validation entre phases

```
   Phase N
      │  travail + commits
      ▼
  Checkpoints N verts ?  ──non──►  rester en Phase N (règle d'or)
      │ oui
      ▼
  CLOSE_PHASE_N : PHASE_N_REPORT.md + handoffs/PHASE_N_TO_(N+1).md
                 + LESSONS_LEARNED.md mis à jour
      │
      ▼
  START_PHASE_(N+1) : lecture handoff + brief, création TopOptP(N+1)/
```
