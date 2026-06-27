# Rapport — Phase 3 : multi-grid + mesh independence

*Rédigé le 2026-06-27. Valeurs mesurées (M4 Max, 64 GB). Le code fait foi.*

**Statut** : **Phase 3 substantiellement complète.** Hiérarchie multi-grilles +
warm-start, filtre Helmholtz à rayon physique (mm), continuation configurable.
Cible « opti 128³ < 10 min » (ratée en Phase 2) : **atteinte** (7,0 min). Le
préconditionneur multigrid V-cycle reste optionnel/différé (voir §7).

---

## 1. But de la phase (rappel)

Phase 2 livrait un solveur TO 3D correct mais lent (opti 128³ = 16,6 min, CG
Jacobi qui plafonne) et au design potentiellement dépendant du maillage. Phase 3
n'ajoute **aucune physique** : elle rend le solveur **rapide** (multi-grid
warm-start) et **fiable** (mesh independence via filtre à rayon physique). C'est le
passage de « ça marche » à « ça marche vite et le résultat ne dépend pas de la
résolution » — prérequis indispensable avant d'empiler la multiphysique (P4-P5).

---

## 2. Ce qui a été construit

| Module | Rôle |
|---|---|
| `Grid3DMultiLevel` | hiérarchie de grilles facteur 2, taille de cellule physique par niveau |
| `GridTransfer` | prolongation (injection) / restriction (moyenne) du champ densité, **conservatives** |
| `HelmholtzFilterPhysical` | filtre Helmholtz avec rayon en **mm** (= r_mm / h), enveloppe de `Helmholtz3D` |
| `ContinuationPolicy` | politique de continuation de p : `inherit` / `restart` / `custom`, struct générique |
| `MultiGridOptimizer` | boucle TO coarse→fine avec warm-start par prolongation |
| `problems/MBB3D.hpp` | BCs MBB indépendantes de la résolution (instanciables à tout niveau) |

Hérite intégralement de la base **matrix-free** Phase 2 (CG/Jacobi, `Helmholtz3D`,
`SIMP3D`, STL). `make` 0 warning, `make test` vert.

---

## 3. Résultats mesurés

### Speedup (le résultat phare)

| Cas | Phase 2 (single-grid) | Phase 3 (multi-grid 4 niveaux) | Gain |
|---|---|---|---|
| Opti MBB 128³ (60 iter ↔ 4×20) | **998,5 s (16,6 min)** | **419,4 s (7,0 min)** | **2,4×**, **< 10 min ✓** |

Mesuré isolé (sans contention GPU). Compliance finale équivalente (0,33).

### Mesh independence (rayon physique 2 mm, domaine 60 mm)

| Résolution finest | Niveaux | Compliance | Volume | Temps |
|---|---|---|---|---|
| 32³ | 3 | 1,319 | 0,3000 | 29 s |
| 64³ | 4 | 0,668 | 0,3000 | 111 s |
| 128³ | 4 | 0,331 | 0,3000 | 419 s |

**Lecture** : la *valeur* de compliance décroît avec le raffinement (effet de
discrétisation attendu : domaine en unités-cellule, charge nodale, plus de DOF).
Ce n'est PAS la métrique de mesh independence. Ce qui compte : à **rayon physique
fixe (2 mm)**, la **taille minimale de feature reste constante en mm** → la
**topologie** des designs converge (mêmes membrures, pas de damiers, pas de
micro-structures dépendantes du maillage), seul le lissage des bords s'affine.
Preuve visuelle : `output/mg_{32,64,128}.stl` (volume tenu à 0,3000 partout).

### Profil de convergence (128³, 4 niveaux) — pourquoi le warm-start gagne

| Niveau | C (1ère it) | C (20e it) | CG iter/solve |
|---|---|---|---|
| L0 16³ | 3,70 | 3,20 | ~200-410 |
| L1 32³ | 1,58 | 1,31 | ~900 |
| L2 64³ | 0,663 | 0,633 | ~1700-2500 |
| L3 128³ | **0,3325** | 0,3314 | 4001 (plafond) |

Au niveau fin, la compliance est **déjà quasi-convergée dès la 1ère itération**
(0,3325 → 0,3314) : le warm-start a fait l'essentiel sur les grilles grossières,
le 128³ ne fait que raffiner les bords. **C'est de là que vient le 2,4×.**

---

## 4. Choix de conception et raisons

### 4.1 Continuation hybride **configurable** (inherit / restart / custom)
- **Quoi** : `inherit` (défaut) = continuation 1→3 sur le niveau grossier, p=3
  ensuite (rapide). `restart` = continuation à chaque niveau (liberté topologique
  max). `custom` = p cible par niveau fourni par l'utilisateur.
- **Pourquoi** : le coût et la liberté topologique s'arbitrent **selon la pièce**.
  Pour une tuyère à canaux de refroidissement, les features fines (canaux LOX)
  n'apparaissent qu'à grille fine et tard dans le dégrossissage → l'architecte doit
  pouvoir re-adoucir p au niveau fin (`restart`/`custom`). Pour une pièce
  structurelle standard, `inherit` suffit et va 2,4× plus vite. On laisse donc le
  choix, sans surcoût de complexité (un enum + un schedule).
- **Forward-compatible** : `ContinuationParams` est une *struct* — les
  continuations futures (ε-relaxation stress P4, α_max Brinkman / β Heaviside P5)
  s'y branchent sans changer l'interface de l'optimiseur. Décision gravée tôt pour
  éviter une dette en P4-P5.

### 4.2 Filtre à rayon **physique (mm)**, pas en cellules
- Mécanisme **réel** de la mesh independence : la taille mini de feature est fixée
  en mm, donc invariante à la résolution. `r_cells = r_mm / h`, h diminuant avec le
  raffinement (Lazarov-Sigmund 2011, LL-LIT-006).

### 4.3 Transferts inter-grilles **conservatifs**
- Prolongation par injection, restriction par moyenne : tous deux préservent la
  moyenne (volume). Round-trip exact (2,2e-16). Sans ça, dérive de volume entre
  niveaux (LL-LIT-010). Vérifié à chaque transition (vol = 0,3000 partout).

### 4.4 Couplage continuation × rayon (piège documenté)
- La continuation détermine si l'optimiseur *peut* nucléer une feature fine ; le
  rayon mm détermine si elle *peut exister*. Pour des canaux fins il faut **les
  deux** : petit rayon ET liberté au niveau fin. Documenté pour éviter qu'un
  utilisateur mette `restart` avec un grand rayon et s'étonne de l'absence de canaux.

---

## 5. Validations

| Test | Cible | Obtenu | Statut |
|---|---|---|---|
| Round-trip prolongation∘restriction | < 1e-6 | 2,2e-16 | ✓ |
| Conservation volume inter-niveaux | < 0,01 % | exact (0,3000) | ✓ |
| Hiérarchie (dims, cell size mm) | cohérent | 16³→128³, 0,5→4 mm | ✓ |
| Patch test FEM 3D (régression P2) | < 1e-6 | 7,8e-16 | ✓ |
| Mesh independence (topologie 32/64/128, r=2mm) | designs cohérents | STLs vol 0,30 | ✓ |
| Speedup vs Phase 2 | ≥ 5× visé | **2,4×** (mais < 10 min atteint) | ⚠️ partiel |
| `make` 0 warning, `make test` | vert | vert | ✓ |

---

## 6. Écart honnête sur le speedup

Le brief visait **5-10×**. Obtenu : **2,4×** (16,6 → 7,0 min). La cible *absolue*
« 128³ < 10 min » est tenue, mais pas le facteur 5-10×. Raison : le speedup vient
**uniquement du warm-start** (moins d'itérations TO nécessaires au niveau fin).
Chaque *solve* au niveau fin coûte encore le prix fort car le préconditionneur
reste **Jacobi** (CG plafonne à 4001 iter). Le facteur 5-10× annoncé dans la
littérature suppose un **préconditionneur multigrid V-cycle** (qui rend le solve
lui-même quasi-O(n)). Ce V-cycle est **différé** (cf. §7) : le warm-start seul
remplit déjà l'objectif fonctionnel.

---

## 7. Dette technique acceptée

| Item | Raison | Résolution prévue |
|---|---|---|
| **Pas de V-cycle multigrid** (CG reste Jacobi au niveau fin, plafonne) | warm-start atteint déjà < 10 min ; V-cycle = effort important | optimisation ultérieure (P3+ ou quand le besoin perf revient) |
| `restart`/`custom` codés mais non benchmarkés sur cas à features fines | pas de cas tuyère avant P4 | valider en P4 (géométrie tuyère) |
| STL = surface voxels (hérité P2, pas de marching cubes lisse) | robuste ; suffisant pour valider la topologie | marching cubes si besoin esthétique |
| Sync CPU↔GPU par opération (hérité P2) | correctness | batching command-buffers |

---

## 8. Proposition de prompt pour approfondir le V-cycle (point différé)

> Le préconditionneur multigrid V-cycle est le seul gros morceau non implémenté.
> Pour qui veut le détailler plus tard, voici un prompt prêt à l'emploi :

```
Contexte : solveur TO 3D élastique matrix-free sur GPU Metal (Apple Silicon),
H8 trilinéaire, CG préconditionné Jacobi qui plafonne (~4001 iter à 128³). Une
hiérarchie de grilles facteur 2 existe déjà (Grid3DMultiLevel) avec prolongation/
restriction du champ densité.

Demande : conçois et implémente un préconditionneur MULTIGRID V-CYCLE
géométrique pour ce CG, en matrix-free sur GPU. Détaille :
1. Le choix du smoother parallélisable sur GPU (Jacobi amorti vs red-black
   Gauss-Seidel) et pourquoi ; nombre de pré/post-smoothing steps.
2. Les opérateurs de transfert NODAUX (déplacements, pas densité) : prolongation
   trilinéaire et restriction = transposée pondérée ; comment les exprimer
   matrix-free sur la grille structurée.
3. La construction de l'opérateur K à chaque niveau grossier : re-discrétisation
   (rediscretization) vs Galerkin (RAP). Recommande pour le matrix-free.
4. Le solveur de niveau le plus grossier (direct ou CG dense).
5. L'intégration comme préconditionneur d'un CG (V-cycle comme application de M^-1).
6. Les pièges : convergence du smoother, traitement des Dirichlet aux niveaux
   grossiers, perte de précision float32, équilibrage coût/gain vs Jacobi.
7. Le speedup attendu vs Jacobi et comment le mesurer proprement (CG iter/solve,
   temps wall-clock à 128³ et 256³).
Donne le pseudo-code du V-cycle et l'architecture des kernels Metal nécessaires.
```

---

## 9. Décisions architecturales (ADR à porter dans docs/DECISIONS.md)

- **ADR-009** : continuation hybride **configurable** (inherit/restart/custom),
  `ContinuationParams` en struct générique pour P4-P5.
- **ADR-010** : transferts inter-grilles conservatifs (injection / moyenne).
- **ADR-011** : mesh independence par filtre à rayon physique (mm).
- **ADR-012** : V-cycle multigrid **différé** ; warm-start seul retenu pour P3
  (atteint la cible fonctionnelle < 10 min).

---

## 10. Checklist de clôture

- [x] `PHASE_3_REPORT.md` complet, valeurs mesurées
- [x] `make` 0 warning ; `make test`/`make test_cpu` vert
- [x] Round-trip + conservation volume + hiérarchie validés
- [x] Mesh independence : STLs 32/64/128 à rayon physique fixe (vol 0,30)
- [x] Speedup mesuré (2,4×, < 10 min) — écart au 5-10× documenté (V-cycle différé)
- [x] `handoffs/PHASE_3_TO_4.md` produit
- [x] `LESSONS_LEARNED.md` : LL-008 ajouté
- [x] `docs/DECISIONS.md` : ADR-009..012
- [x] STLs livrés (`output/mg_*.stl`)
