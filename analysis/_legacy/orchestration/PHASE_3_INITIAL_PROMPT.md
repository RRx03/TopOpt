# PHASE 3 — Prompt initial : Multi-grid uniforme + mesh independence

## Instruction à Claude au démarrage de Phase 3

---

## ÉTAPE 0 — Lire ces fichiers AVANT TOUT (ordre obligatoire)

1. `../orchestration/MASTER_CLAUDE.md` — règles communes, format de réponse
2. `../orchestration/LESSONS_LEARNED.md` — erreurs accumulées à éviter
3. `../TopOptP1/TRANSITIONS.md` — section "Phase 3" complète
4. `../TopOptP2/CLAUDE.md` — état du code en sortie de Phase 2
5. Ce fichier — objectif et priorités Phase 3

**Note sur Phase 2** : si `../TopOptP2/PHASE_2_REPORT.md` n'existe pas encore,
signaler l'absence avant de continuer. Phase 3 ne peut démarrer que si les
acquis Phase 2 sont validés (cf. checkpoints `TRANSITIONS.md` section Phase 2).

---

## Contexte

Phase 2 a produit un solveur TO 3D sur GPU Metal (hexaèdres H8, CG préconditionné
Jacobi, assembly CSR sur GPU). La performance sur 128³ est acceptable mais
le solveur recalcule chaque grille de zéro et la mesh dependence n'est pas démontrée.

Phase 3 corrige ça : hiérarchie de grilles, warm-start, filtre Helmholtz
avec rayon physique en mm.

---

## Objectif Phase 3

Construire la hiérarchie multi-grilles et démontrer la mesh independence.
Cette phase n'ajoute PAS de physique nouvelle — elle rend le solveur
industriellement viable en résolution et en coût computationnel.

---

## Acquis attendus en sortie (tous obligatoires)

- [ ] Hiérarchie de grilles : 3-4 niveaux (ex. 32³ → 64³ → 128³ → 256³)
- [ ] Opérateurs d'interpolation (prolongation) et restriction implémentés et testés
- [ ] Warm-start : ρ à niveau N+1 initialisé par interpolation conservative de ρ_N
- [ ] Filtre Helmholtz avec rayon physique r en mm (indépendant de h) — cf. LL-LIT-006
- [ ] Démonstration mesh independence : design 64³ ≡ design 256³ à raffinement près
- [ ] Speedup total : 5-10× sur l'optimisation complète vs Phase 2 baseline
- [ ] Pipeline complet : JSON → optim multi-grid → STL, end-to-end
- [ ] Test filtre physique : modifier r de 1mm à 2mm → features plus grosses, prévisiblement

---

## Priorités strictes de la première session

**Ce qu'on fait en session 1 :**
1. Présenter le plan d'architecture multi-grid (structures de données,
   opérateurs, API) AVANT de coder
2. Implémenter `Grid3DMultiLevel` : conteneur de la hiérarchie, N niveaux
3. Opérateurs prolongation/restriction sur GPU (kernels Metal)
4. Test unitaire : prolongation puis restriction = identité à ε près
5. Filtre Helmholtz avec rayon r en mm : modifier `HelmholtzFilter` (ou
   nouvelle classe) pour prendre r_mm et h_mm, calculer r_cells = r_mm/h_mm

**Ce qu'on ne touche PAS en session 1 :**
- La loop TO complète (attendre que les opérateurs soient validés)
- La visualisation STL (inchangée depuis Phase 2)
- Le solveur CG (inchangé, les opérateurs multi-grilles viennent après)

---

## Pièges prioritaires à gérer dès le départ

### LL-LIT-006 : Rayon filtre en mm, pas en cellules
Implémenter dès le premier commit. Toute la Phase 3 est invalide si le
filtre reste en cellules.

### LL-LIT-010 : Conservation volume entre niveaux
L'interpolation `ρ_fin = ρ_grossier[cellule parente]` ne conserve pas le
volume. Implémenter interpolation conservative (trilinéaire pondérée par
volume). Tester conservation à 0.01%.

### Continuation de p entre niveaux (Phase 3, piège de TRANSITIONS.md)
Sur grille 64³ : p = 1 → 2 → 3 sur 30 iter chacun.
Sur grille 128³ (warm-start depuis 64³) : quelle valeur de p ?
Option A : repartir à p=1 (safe, mais cher).
Option B : hériter le p final de la grille précédente (rapide, risque minimum local).
**Décision à prendre avant de coder la loop multi-grid. Documenter dans DECISIONS.md.**

---

## Critères de validation fin de session 1

- Test unitaire prolongation/restriction passe (ε < 1e-6 double, 1e-4 float)
- Filtre Helmholtz avec r_mm configurable : modifier r → design différent
  et prévisible (features plus grosses si r grand)
- Conservation du volume avant/après interpolation : erreur < 0.01%
- Aucun warning de compilation

---

## Drapeaux rouges spécifiques Phase 3

- [ ] Design visuellement différent à 64³ et 256³ avec même r_mm →
      STOP, mesh dependence non résolue
- [ ] Volume qui dérive entre niveaux sans correction →
      STOP, bug interpolation conservative
- [ ] V-cycle qui ne converge pas → OK de laisser en fallback CG+Jacobi,
      mais DOCUMENTER explicitement dans TRANSITIONS.md
- [ ] Speedup < 2× vs Phase 2 → investiguer avant d'avancer

---

## Références canoniques Phase 3

- Aage-Andreassen-Lazarov 2015, *Struct. Multidisc. Optim.* 51:565
  (multigrid + large-scale)
- Lazarov-Sigmund 2011, *Int. J. Numer. Methods Eng.* 86:765
  (filtre Helmholtz physique)
- Aage et al. 2017, *Nature* 550 (large-scale, préconditionneur multigrid)

---

## Architecture TopOptP3/ cible

Copier la structure de TopOptP2/ et ajouter :
```
src/
├── core/
│   ├── Grid3DMultiLevel.{hpp,cpp}     // NEW : hiérarchie N niveaux
├── fem/
│   ├── ProlongationOperator.{hpp,cpp} // NEW : ρ_coarse → ρ_fine
│   └── RestrictionOperator.{hpp,cpp}  // NEW : ρ_fine → ρ_coarse
├── filter/
│   └── HelmholtzFilterPhysical.{hpp,cpp} // NEW : r en mm
└── topopt/
    └── MultiGridOptimizer.{hpp,cpp}   // NEW : loop multi-grid

shaders/
├── prolongation.metal                 // NEW
└── restriction.metal                  // NEW

tests/
├── test_prolongation.cpp              // NEW
├── test_restriction.cpp               // NEW
├── test_filter_physical.cpp           // NEW
└── test_mesh_independence.cpp         // NEW : 64³ vs 256³
```
