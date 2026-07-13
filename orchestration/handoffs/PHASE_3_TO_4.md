# Handoff PHASE 3 → PHASE 4

*Rédigé le 2026-06-27 à la clôture de Phase 3 (code réel, mesuré). Le code fait
foi : `TopOptP3/`, `TopOptP3/PHASE_3_REPORT.md`.*

---

## État du code en sortie de Phase 3

**Phase 3 substantiellement complète** : solveur TO 3D **multi-grid warm-start**
sur la base matrix-free de Phase 2. `make` 0 warning, `make test` vert.

```
TopOptP3/src/  (en plus de l'hérité Phase 2)
├── core/Grid3DMultiLevel.hpp        # hiérarchie ×2, cell size physique/niveau
├── topopt/GridTransfer.{hpp,cpp}    # prolongation/restriction densité conservatives
├── topopt/ContinuationPolicy.hpp    # inherit/restart/custom (struct générique)
├── topopt/MultiGridOptimizer.{hpp,cpp} # loop coarse->fine warm-start
├── filter/HelmholtzFilterPhysical.hpp  # rayon en mm (mesh-independent)
└── problems/MBB3D.hpp               # BCs MBB indépendantes de la résolution
```

### Acquis validés (mesuré)
- Round-trip prolongation∘restriction : 2,2e-16 ; conservation volume exacte.
- **Mesh independence** : MBB 3D 32³/64³/128³ à rayon physique 2 mm, vol 0,30
  partout, topologie cohérente (STLs `output/mg_*.stl`).
- **Speedup** : opti 128³ **998 s → 419 s (2,4×, < 10 min)**. Vient du warm-start
  (design quasi-convergé dès les niveaux grossiers).
- Continuation configurable (inherit/restart/custom) opérationnelle.

### Décisions (ADR-009..012, cf. report §9)
- Continuation hybride **configurable** ; `ContinuationParams` = struct générique
  prête pour les continuations P4-P5.
- Transferts conservatifs ; filtre rayon mm ; **V-cycle multigrid différé**
  (warm-start seul atteint la cible).

### Réserve transmise
- Le speedup est de 2,4× (pas 5-10×) : le CG reste **Jacobi** (plafonne à 4001 iter
  au niveau fin). Le V-cycle (prompt d'approfondissement fourni dans le report §8)
  est l'optimisation perf restante — à reprendre si le coût redevient bloquant.

---

## Prérequis Phase 4 — SATISFAITS

- [x] Solveur 3D GPU multi-grid validé, mesh independence démontrée
- [x] Filtre Helmholtz à rayon physique (mm) opérationnel
- [x] Pipeline complet jusqu'au STL
- [x] Adjoint compliance (mono-bloc) propre — base de l'extension multi-bloc P4
- [x] Infrastructure de continuation générique (struct) prête pour ε-relaxation, MMA

---

## Modifications requises pour Phase 4

| Composant | Phase 3 | Phase 4 |
|---|---|---|
| Physique | élasticité seule | + **conduction thermique** stationnaire |
| Couplage | aucun | **thermo-élastique faible** (ε_th = α(T−T_ref) → F_thermal) |
| Objectif/contraintes | compliance + volume | **masse** sous **von Mises** (p-norm) + **T_max** |
| Optimiseur | OC (bissection) | **MMA** (Svanberg 1987) — multi-contraintes |
| Adjoint | compliance mono-bloc | **adjoint 2 blocs** (méca + thermo), **validé par DF** |
| Solveur linéaire | CG (SPD) | méca SPD + thermo SPD ; si couplage one-way asymétrique → BiCGStab/GMRES |
| Géométrie | cartésienne 3D | **2D axisymétrique** (r,z), facteurs 2πr, singularité r=0 |
| Continuation | p (SIMP) | + **ε-relaxation** stress (brancher dans `ContinuationParams`) |

---

## Architecture cible Phase 4

```
TopOptP4/src/  (copie P3 + ajouts)
├── physics/ThermalSolver.{hpp,cpp}        # conduction stationnaire GPU
├── physics/ThermoElasticCoupling.{hpp,cpp}# ε_th -> F_thermal
├── topopt/StressConstraint.{hpp,cpp}      # von Mises + ε-relaxation
├── topopt/PNormAggregation.{hpp,cpp}      # σ_max approché
├── topopt/MMAOptimizer.{hpp,cpp}          # Svanberg 1987
├── adjoint/MultiBlockAdjoint.{hpp,cpp}    # adjoint méca+thermo
├── core/Grid2DAxi.{hpp,cpp}               # grille (r,z) axisym
└── tests/{test_thermal_*,test_adjoint_fd,test_nozzle_2d_axi}.cpp
```

---

## Pièges spécifiques Phase 4 (NIVEAU CRITIQUE)

- **LL-LIT-001** — stress singularity : ε-relaxation dès la 1ère version des contraintes.
- **LL-LIT-007** — adjoint multi-bloc : **validation DF obligatoire** (10×10, 1e-5)
  avant tout run grande taille.
- **LL-LIT-011** — asymétrie K_couplée → BiCGStab/GMRES (décider avant d'assembler).
- **LL-008** (déjà rencontré) — clamper toute densité avant `pow` à exposant
  fractionnaire ; borner toute bissection. La continuation P4 (p + ε) multiplie ces
  cas → vigilance.
- Singularité axisymétrique r=0 : nœuds à r=ε>0 ou formulation spéciale.

---

## Validations obligatoires fin de Phase 4

- Patch test thermique (flux uniforme) < 1e-10 ; plaque gradient T linéaire < 0,1 %.
- **Adjoint 2 blocs par DF (10×10)** < 1e-5 — gate bloquant.
- p-norm vs max réel de σ_vM : écart contrôlé, monotone en p.
- Cas tuyère 2D axi : épaississement au col (sinon bug).
- Patch test FEM méca conservé ; `make` 0 warning.

---

## Référence canonique

Pedersen-Pedersen 2010 (thermo-élastique) ; Le-Norato-Bruns 2010 (stress, p-norm) ;
Duysinx-Bendsøe 1998 (ε-relaxation) ; Svanberg 1987 (MMA).
