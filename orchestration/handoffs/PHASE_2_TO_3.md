# Handoff PHASE 2 → PHASE 3

*Mis à jour le 2026-06-26 à la clôture de Phase 2 (code réel, mesuré). Le code
fait foi : `TopOptP2/`, `TopOptP2/PHASE_2_REPORT.md`.*

---

## État du code en sortie de Phase 2 (réel)

**Phase 2 substantiellement complète** : solveur TO 3D **matrix-free** sur GPU
Metal, validé. `make` 0 warning, `make test` tout vert.

```
TopOptP2/src/
├── core/Grid3D.hpp               # grille H8 structurée (row-major)
├── fem/H8Element.{hpp,cpp}       # KE0 24×24 + Le/Me 8×8 (Gauss 2×2×2)
├── fem/FEM3D.{hpp,cpp}           # référence CPU (LDLT) pour validation
├── topopt/SIMP3D.{hpp,cpp}       # SIMP + OC (bissection, volume-preserving)
├── filter/Helmholtz3D.{hpp,cpp}  # filtre PDE GPU matrix-free scalaire
├── gpu/CGSolver3D.{hpp,cpp}      # CG matrix-free Jacobi (élasticité) GPU
└── io/STLExporter.{hpp,cpp}      # surface voxels → STL
shaders/fem3d.metal              # matvec/diag élastique + Helmholtz + vecops + ce
```

### Acquis validés (mesuré)
- Patch test FEM 3D (traction constante) : 7.8e-16 (double).
- Cantilever 3D : ratio FE/Euler-Bernoulli 0.977.
- CG GPU vs CPU direct : relres 8.8e-7, écart 3.1e-4 (float vs double).
- MBB 3D : compliance monotone, volume tenu exactement.
- **Solve 128³ (6.44M DOF) : 6.4 s, 1516 iter CG.** Opti 128³ 60-iter : 16.6 min.

### Décisions structurantes (ADR-005..008, cf. report §11)
- **Matrix-free** (pas de K assemblée) : K·u recalculé par node-gather, scalable
  128³ (~150 MB), sans atomics. *(Diverge du plan CSR initial — décision validée.)*
- **Emin = 1e-4** (pas 1e-9) : requis par le CG itératif float32 (LL-006).
- Filtre GPU matrix-free scalaire ; OC exploite la conservation de volume (LL-007).
- STL = surface des voxels (marching cubes lisse → Phase 3).

---

## Prérequis Phase 3 — désormais SATISFAITS

- [x] Solveur 3D GPU complet et validé (matvec/CG/Jacobi/patch/cantilever/MBB)
- [x] Filtre Helmholtz GPU opérationnel (rayon en cellules)
- [x] Pipeline complet jusqu'au STL
- [x] Adjoint compliance (mono-bloc) compris (base de Phase 4)

> Le solveur **matrix-free** simplifie Phase 3 : prolongation/restriction opèrent
> sur ρ (élément) et sur les vecteurs nodaux, sans manipuler de structure CSR.

---

## Modifications requises pour Phase 3

| Composant | Phase 2 | Phase 3 |
|---|---|---|
| Solveur linéaire | CG + **Jacobi** (itérations croissent : 1516→4001 plafond quand le design durcit) | CG + **warm-start** + préconditionneur **multigrid V-cycle** |
| Stratégie TO | une grille fine | hiérarchie 32³→64³→128³(→256³) |
| Filtre Helmholtz | rayon en **cellules** | rayon **physique en mm** (mesh-independent) |
| Visualisation | surface voxels | marching cubes (iso ρ=0.5) lisse |
| Validation | convergence 128³ | **mesh independence** multi-résolution |
| Perf cible | solve 6.4 s ; opti 16.6 min | **opti 128³ < 10 min** via multigrid (5-10× speedup) |

**Le point dur de Phase 3** : faire tomber le coût CG. À 128³, Jacobi plafonne à
4001 iter en fin d'optimisation → c'est *la* motivation du multigrid.

---

## Architecture cible Phase 3

```
TopOptP3/  (copie de TopOptP2 + ajouts)
├── src/core/Grid3DMultiLevel.{hpp,cpp}        # hiérarchie N niveaux
├── src/fem/ProlongationOperator.{hpp,cpp}     # grossier → fin (conservative)
├── src/fem/RestrictionOperator.{hpp,cpp}      # fin → grossier
├── src/filter/HelmholtzFilterPhysical.{hpp,cpp} # rayon mm (étend Helmholtz3D)
├── src/topopt/MultiGridOptimizer.{hpp,cpp}    # loop multi-grid + warm-start
├── src/gpu/MultigridPrecond.{hpp,cpp}         # V-cycle (smoother red-black GS)
├── shaders/{prolongation,restriction,smoother}.metal
└── tests/{test_prolongation,test_restriction,test_filter_physical,test_mesh_independence}.cpp
```

---

## Pièges spécifiques Phase 3 (renvois LESSONS_LEARNED)

- **LL-LIT-006** — rayon filtre en mm (pas cellules), sinon mesh independence échoue.
- **LL-LIT-010** — interpolation conservative, sinon dérive de volume entre niveaux.
- **LL-006** — garder Emin borné (1e-4) ; le multigrid pourra autoriser plus bas.
- Continuation de p entre niveaux : décision à documenter (`docs/DECISIONS.md`).
- V-cycle non convergent : fallback CG+Jacobi+warm-start (déjà fonctionnel), à documenter.

---

## Validations obligatoires fin de Phase 3

- Round-trip prolongation∘restriction : < 1e-6 (double) / 1e-4 (float)
- Conservation volume entre niveaux : < 0.01 %
- Mesh independence MBB 3D (64³ vs 128³ vs 256³, r mm fixe) : features identiques
- Speedup ≥ 5× vs Phase 2 single-grid (mesuré) ; **opti 128³ < 10 min**
- Sensibilité au rayon (1mm vs 2mm) : variation prévisible
- Patch test FEM 3D conservé : < 1e-6 (float)

---

## Référence canonique

Aage, Andreassen, Lazarov 2015 (*Struct. Multidisc. Optim.* 51:565, multigrid +
large-scale, matrix-free) ; Lazarov, Sigmund 2011 (filtre Helmholtz).
