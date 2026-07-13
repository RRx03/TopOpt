# PHASE 4 — Prompt initial : Couplage thermo-élastique + von Mises + 2D axi

## Instruction à Claude au démarrage de Phase 4

---

## ÉTAPE 0 — Lire ces fichiers AVANT TOUT (ordre obligatoire)

1. `../orchestration/MASTER_CLAUDE.md` — règles communes, format de réponse
2. `../orchestration/LESSONS_LEARNED.md` — erreurs accumulées à éviter
3. `../TopOptP1/TRANSITIONS.md` — section "Phase 4" complète (fragilités CRITIQUES)
4. `../TopOptP3/CLAUDE.md` — état du code en sortie de Phase 3
5. `../TopOptP3/PHASE_3_REPORT.md` — rapport Phase 3, acquis validés
6. Ce fichier — objectif et priorités Phase 4

---

## Contexte

Phase 3 a produit un solveur TO 3D multi-grilles avec mesh independence démontrée
et filtre Helmholtz physique. Phase 4 ajoute la première physique couplée :
thermique + mécanique. C'est ici que le projet devient spécifique à la propulsion
spatiale.

---

## Objectif Phase 4

Premier couplage physique : ajouter la conduction thermique stationnaire.
Première vraie contrainte ingénieur : von Mises < σ_yield.
Première adaptation aérospatiale : géométrie 2D axisymétrique.

---

## Acquis attendus en sortie (tous obligatoires)

- [ ] Solveur de conduction thermique stationnaire (équation de la chaleur)
      sur la même grille que la mécanique
- [ ] Couplage thermo-élastique faible : T(x) → ε_thermique = α(T-T_ref)
      → contrainte thermique additionnelle dans K_méca
- [ ] Géométrie 2D axisymétrique : intégrales en 2πr, formulation FEM en (r,z)
- [ ] Calcul de la contrainte de von Mises par élément
- [ ] Agrégation p-norm des contraintes : σ_vM_max ≈ (Σ σ_i^p)^(1/p), p=8-12
- [ ] Adjoint multi-block (méca + thermo) validé par différences finies
- [ ] Update MMA (Svanberg 1987) à la place de OC
- [ ] Cas test tuyère 2D axi :
      - Pression intérieure 80 bar + flux thermique pariétal 10 MW/m²
      - Objectif : minimiser masse sous von_Mises < σ_yield ET T_paroi < T_max
      - Vérification physique : paroi plus épaisse au col (pic thermique+mécanique)
- [ ] Validation adjoint par DF sur sub-cas 10×10 : matching à 1e-5

---

## Priorités strictes de la première session

**Ce qu'on fait en session 1 :**
1. Présenter le plan d'architecture du couplage thermo-élastique AVANT de coder
   (solveur thermique indépendant d'abord, puis couplage)
2. Implémenter le solveur de conduction thermique sur GPU (même format CSR,
   même CG préconditionné) en tant que second bloc indépendant
3. Test analytique : plaque homogène sous gradient de température linéaire,
   T(x) analytique vs FEM
4. Valider la résolution thermique AVANT de toucher au couplage

**Ce qu'on ne touche PAS en session 1 :**
- Le couplage thermo-élastique (vient après validation thermique seule)
- von Mises / p-norm (vient après couplage)
- MMA (vient après les sensibilités)
- Géométrie 2D axi (vient après validation 3D du couplage)

---

## Pièges prioritaires — NIVEAU CRITIQUE

### LL-LIT-001 : Stress singularity (DRAPEAU ROUGE PHASE 4)
Quand ρ → 0 : σ_vM/ρ → ∞ artificiellement. Sans correction, l'optimiseur
refuse de mettre du vide. **Bug qui fait perdre 2 semaines si découvert tard.**
- Implémenter ε-relaxation (Duysinx 1998) dès la première version des contraintes
- Ne pas coder von Mises sans cette correction

### LL-LIT-007 : Validation adjoint multi-bloc OBLIGATOIRE
L'adjoint Phase 4 couvre la chaîne :
ρ → K_méca → U → σ (mécanique)
ρ → K_thermo → T → ε_thermique → σ (thermique)
**Valider par DF sur 10×10 avant tout run grande taille.**
Tolérance : matching à 1e-5 minimum.

### LL-LIT-011 : Asymétrie K_couplée
Le couplage one-way T → σ rend la matrice globale non-symétrique.
CG ne marche plus. Passer à BiCGStab ou GMRES.
**Choisir avant d'assembler la matrice couplée. Documenter dans DECISIONS.md.**

### Singularité axisymétrique à r=0
FEM en (r,z) : intégrales en 2πr → singularité à r=0.
Solution : nœuds à r=ε > 0 (pas de nœud sur l'axe) ou formulation spéciale.
**Décider et documenter avant de coder la géométrie axi.**

---

## Critères de validation fin de session 1 (thermique seule)

- Plaque sous gradient linéaire : T_FEM vs T_analytique < 0.1%
- Test patch thermique : flux uniforme reproduit à 1e-10
- Aucun warning de compilation
- Solveur thermique GPU indépendant du solveur mécanique (deux blocs séparés)

---

## Drapeaux rouges spécifiques Phase 4

- [ ] Adjoint non validé par DF avant run en grande taille →
      STOP, ne pas continuer sans cette vérification
- [ ] Stress singularity non traitée (ε-relaxation absente) →
      STOP, l'optimiseur produira un résultat aberrant
- [ ] Design tuyère : paroi uniforme (pas d'épaississement au col) →
      STOP, bug dans le couplage ou les BCs thermiques
- [ ] MMA qui oscille → investiguer GCMMA (Svanberg 2002) comme fallback
- [ ] K_couplée symétrique alors que le couplage est one-way →
      vérifier la formulation, c'est probablement un oubli de terme

---

## Références canoniques Phase 4

- Pedersen-Pedersen 2010, *Struct. Multidisc. Optim.* 42:681 (thermo-élastique)
- Le-Norato-Bruns 2010, *Struct. Multidisc. Optim.* 41:605 (stress-constrained TO)
- Duysinx-Bendsøe 1998 — ε-relaxation pour stress singularity
- Bruggi 2008 — qp-approach, alternative à ε-relaxation
- Svanberg 1987, *Int. J. Numer. Methods Eng.* 24:359 (MMA original)
- Svanberg 2002 — GCMMA, fallback si MMA oscille

---

## Architecture TopOptP4/ cible

Copier la structure de TopOptP3/ et ajouter :
```
src/
├── physics/
│   ├── ThermalSolver.{hpp,cpp}        // NEW : conduction stationnaire GPU
│   └── ThermoElasticCoupling.{hpp,cpp}// NEW : ε_thermo = α(T-T_ref)
├── topopt/
│   ├── StressConstraint.{hpp,cpp}     // NEW : von Mises + ε-relaxation
│   ├── PNormAggregation.{hpp,cpp}     // NEW : σ_vM_max approchée
│   └── MMAOptimizer.{hpp,cpp}         // NEW : Svanberg 1987
├── adjoint/
│   └── MultiBlockAdjoint.{hpp,cpp}    // NEW : adjoint méca + thermo
├── core/
│   └── Grid2DAxi.{hpp,cpp}            // NEW : grille 2D axi (r,z)
└── fem/
    └── ElementMatrixQ1Axi.{hpp,cpp}   // NEW : élément Q1 axisym

shaders/
├── thermal_assembly.metal             // NEW
└── thermal_spmv.metal                 // NEW (ou mutualisé avec mécanique)

tests/
├── test_thermal_patch.cpp             // NEW
├── test_thermal_analytical.cpp        // NEW : gradient linéaire T
├── test_adjoint_fd.cpp                // NEW : DF vs adjoint, 10x10
└── test_nozzle_2d_axi.cpp             // NEW : cas tuyère
```
