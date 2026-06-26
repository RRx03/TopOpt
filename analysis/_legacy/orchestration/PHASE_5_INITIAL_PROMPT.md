# PHASE 5 — Prompt initial : Couplage Stokes + CHT, TO multiphysique

## Instruction à Claude au démarrage de Phase 5

---

## ÉTAPE 0 — Lire ces fichiers AVANT TOUT (ordre obligatoire)

1. `../orchestration/MASTER_CLAUDE.md` — règles communes, format de réponse
2. `../orchestration/LESSONS_LEARNED.md` — erreurs accumulées à éviter
3. `../TopOptP1/TRANSITIONS.md` — section "Phase 5" complète (fragilités TRÈS CRITIQUES)
4. `../TopOptP4/CLAUDE.md` — état du code en sortie de Phase 4
5. `../TopOptP4/PHASE_4_REPORT.md` — rapport Phase 4, acquis validés
6. Ce fichier — objectif et priorités Phase 5

---

## Contexte

Phase 4 a produit un solveur thermo-élastique 2D axi avec contraintes von Mises
et adjoint multi-bloc validé. Phase 5 ajoute la physique fluide : Stokes
incompressible avec Brinkman penalization, couplage CHT (Conjugate Heat Transfer).
C'est le livrable phare différenciateur du projet.

---

## Objectif Phase 5

TO multiphysique fluide-structure-thermique sur une géométrie de cooling jacket.
Un seul framework résout simultanément : quel matériau est solide (structure),
où passe le fluide (canaux), et comment la chaleur est transférée.

---

## Acquis attendus en sortie (tous obligatoires)

- [ ] Solveur Stokes incompressible avec Brinkman penalization :
      α(ρ)(1-ρ)^q × u dans équation de moment, q = 4-8
- [ ] Éléments Taylor-Hood (P2 vitesse, P1 pression) OU Q1-Q1 stabilisé PSPG
- [ ] Solveur système saddle-point Stokes : MINRES, Schur complement, ou Uzawa
- [ ] Couplage CHT : conduction solide + advection-diffusion fluide,
      continuité T et q à l'interface
- [ ] Adjoint triple-couplé (méca + thermo + Stokes) validé par DF
- [ ] Filtre projection Heaviside (Wang-Lazarov-Sigmund 2011) pour binarisation ρ
- [ ] Non-dimensionnalisation complète (pression, vitesse, température, contrainte)
- [ ] Cas test : cooling jacket régénératif pour tuyère méthalox 5 kN
      - Contraintes : T_paroi_chaude < T_max, ΔP_cooling < ΔP_max,
        von_Mises < σ_yield
      - Objectif : minimiser masse
      - Vérification : canaux plus denses au col (pic thermique naturel)
- [ ] Comparaison qualitative à un design Borrvall-Petersson (channel flow)

---

## Priorités strictes de la première session

**Ce qu'on fait en session 1 :**
1. Présenter le plan d'architecture Stokes + CHT (structures de données,
   choix d'éléments, solveur saddle-point) AVANT de coder
2. Choisir explicitement : Taylor-Hood ou Q1-Q1 PSPG → documenter dans DECISIONS.md
3. Implémenter le solveur Stokes seul (sans TO, sans CHT) sur cas Poiseuille
4. Test analytique : écoulement de Poiseuille entre deux plaques,
   u(y) parabole, comparaison FEM vs analytique

**Ce qu'on ne touche PAS en session 1 :**
- Le couplage CHT (vient après Stokes validé)
- La Brinkman penalization (vient après CHT)
- L'adjoint triple-couplé (vient en dernier)

---

## Pièges prioritaires — NIVEAU TRÈS CRITIQUE

### LL-LIT-002 : Inf-sup condition (DÉCISION IMMÉDIATE OBLIGATOIRE)
Mauvais choix d'éléments = oscillations de pression, design absurde.
**Avant de coder une seule ligne de Stokes, décider :**
- Taylor-Hood (P2-P1) : safe, conforme, mais DoF plus nombreux
- Q1-Q1 PSPG stabilisé : même ordre, mais stabilisation nécessaire
**Documenter le choix dans DECISIONS.md avec justification.**

### LL-LIT-004 : Brinkman mal calibrée
α_max trop faible → fuite fluide dans solide.
α_max trop grand → conditionnement K explose.
Sweet spot : α_max = 1e3 à 1e5 selon viscosité.
**Implémenter avec continuation de α_max, tester les bornes.**

### LL-LIT-012 : Non-dimensionnalisation (PRIORITÉ ABSOLUE)
Mélange Pa, m/s, K, MPa → cond > 1e15 → divergence silencieuse.
**Définir les échelles de référence AVANT d'assembler la première matrice :**
```
u_ref = vitesse débitante (m/s)
L_ref = longueur caractéristique (m)
p_ref = μ × u_ref / L_ref (Pa)
T_ref = température ambiante (K)
σ_ref = module de Young (Pa)
```
**Tester que le résidu non-dimensionnel reste dans [1e-12, 1e0].**

### LL-LIT-007 : Adjoint triple-couplé OBLIGATOIRE par DF
La chaîne est : ρ → K_méca + K_thermo + Stokes(α(ρ)) → U, T, (u,p)
Chaque terme doit contribuer au gradient. Un seul oubli = design aberrant.
**Sur 5×5 d'abord : DF vs adjoint à 1e-3 minimum.**
Si ça ne matche pas : **STOP, ne pas avancer.**

---

## Critères de validation fin de session 1 (Stokes seul)

- Poiseuille entre deux plaques : u_max vs analytique < 0.5%
- Test inf-sup : aucune oscillation de pression sur grille uniforme
- Condition aux limites : no-slip sur les parois, débit imposé à l'entrée
- Aucun warning de compilation

---

## Drapeaux rouges spécifiques Phase 5

- [ ] Oscillations de pression → STOP, mauvais choix d'éléments ou PSPG absent
- [ ] Fuite fluide dans solide (design final : fluide traverse la structure) →
      STOP, α_max insuffisant
- [ ] Adjoint triple-couplé non validé avant run cooling jacket →
      STOP absolu
- [ ] Conditionnement matrice > 1e12 → STOP, non-dimensionnalisation manquante
- [ ] Design cooling jacket : canaux uniformes (pas de concentration au col) →
      investiguer, soit bug CHT soit spec cas test incorrecte
- [ ] Temps par itération TO > 1 heure sur 64³ → documenter limitation,
      réduire la taille pour le démo

---

## Limitations documentées (dette technique acceptée)

- Stokes seul (pas Navier-Stokes) : valide uniquement Re << 1 ou canaux lents
- Laminaire uniquement (pas de turbulence)
- Stationnaire uniquement (pas de transitoire)
- Couplage one-way : Stokes ne change pas K_méca (pas d'IFS vrai)
- Ces limitations DOIVENT être documentées dans PHASE_5_REPORT.md et README

---

## Références canoniques Phase 5

- Borrvall-Petersson 2003, *Int. J. Numer. Methods Fluids* 41:77
  (Brinkman pour TO fluide — cas test de validation)
- Dilgen et al. 2018, *Struct. Multidisc. Optim.* 57:1905
  (TO multiphysique fluide-thermique adjointe)
- Alexandersen-Andreasen 2020, *Fluids* 5(1):29
  (review TO fluide — lecture obligatoire)
- Wang-Lazarov-Sigmund 2011 — filtre Heaviside
- Causin-Gerbeau-Nobile 2005 — added-mass instability FSI (Phase 5+)

---

## Architecture TopOptP5/ cible

Copier la structure de TopOptP4/ et ajouter :
```
src/
├── physics/
│   ├── StokesSolver.{hpp,cpp}         // NEW : Stokes + Brinkman
│   ├── CHTCoupling.{hpp,cpp}          // NEW : advection-diffusion couplée
│   └── BrinkmanPenalization.{hpp,cpp} // NEW : α(ρ)
├── fem/
│   ├── ElementTaylorHood.{hpp,cpp}    // NEW : éléments P2-P1 (ou Q1-Q1 PSPG)
│   └── AssemblyStokes.{hpp,cpp}       // NEW : assembly Stokes GPU
├── topopt/
│   ├── HeavisideFilter.{hpp,cpp}      // NEW : Wang-Lazarov-Sigmund 2011
│   └── TripleCoupledObjective.{hpp,cpp}// NEW : J(ρ,U,T,u,p)
├── adjoint/
│   └── TripleBlockAdjoint.{hpp,cpp}   // NEW : adjoint méca+thermo+Stokes
└── nondim/
    └── Scales.{hpp,cpp}               // NEW : échelles de référence

shaders/
├── stokes_assembly.metal              // NEW
├── stokes_spmv.metal                  // NEW
└── advdiff_assembly.metal             // NEW

tests/
├── test_poiseuille.cpp                // NEW : Stokes, validation analytique
├── test_cht_analytical.cpp            // NEW : profil T advection-diffusion 1D
├── test_brinkman_leak.cpp             // NEW : α_max suffisant, pas de fuite
├── test_adjoint_triple_fd.cpp         // NEW : DF vs adjoint 5x5
└── test_cooling_jacket.cpp            // NEW : cas tuyère méthalox
```
