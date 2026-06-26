# Handoff PHASE 2 → PHASE 3

*Reconstruit rétroactivement le 2026-06-26 à partir du code réel
(`analysis/CODE_ANALYSIS.md`) et de `TopOptP2/PHASE_2_REPORT.md`. Le code fait foi.*

---

## ⚠️ AVERTISSEMENT D'HONNÊTETÉ — Phase 2 NON terminée

**Phase 2 est à ~1/9 : seule la fondation Metal est faite.** Ce handoff existe pour
l'outillage documentaire, **mais Phase 3 ne peut pas démarrer réellement** tant que
le solveur 3D GPU n'est pas complet et validé. La règle d'or (ROADMAP, TRANSITIONS)
l'interdit. **Avant de lancer Phase 3 : terminer Phase 2.**

---

## État du code en sortie de Phase 2 (réel)

~243 LOC. Build OK, `make test` → match GPU/CPU exact.

```
TopOptP2/
├── Makefile                       # two-phase : .metal→.metallib puis C++/link
├── src/gpu/
│   ├── MetalContext.{hpp,cpp}     # device, queue, library, pipeline compute
│   └── metal_impl.cpp             # TU unique *_PRIVATE_IMPLEMENTATION
├── shaders/vector_add.metal       # kernel démo
└── tests/test_metal_hello.cpp     # vec_add 1M GPU vs CPU
```

### Acquis réels (1/9)
| Checkpoint Phase 2 | Statut |
|---|:---:|
| Fondation Metal (device/queue/library/pipeline) | ✓ |
| Acquis Phase 1 porté en 3D | ✗ |
| CG préconditionné Jacobi GPU | ✗ |
| Assembly CSR GPU | ✗ |
| SpMV GPU | ✗ |
| Loop CG complète GPU | ✗ |
| Marching cubes → STL | ✗ |
| Cantilever 3D, MBB 3D | ✗ |
| Benchmark 128³ < 10 min | ✗ |

### Faits techniques (fondation)
- `topopt::gpu::MetalContext` : ref counting manuel (non-ARC), forward decls MTL::,
  `newLibrary("build/shaders.metallib")`, `StorageModeShared`.
- Hardware vérifié : M4 Max, Apple9, 55.66 GB working set.
- `vec_add` 1M floats : max|gpu−cpu| = 0.000e+00.
- ADR-001..004 (vierge Metal-only, metal-cpp local, build 0 warning, float démo).

---

## Reste-à-faire Phase 2 (à terminer AVANT Phase 3)

| Composant | État | Prochaine étape |
|---|---|---|
| Élément H8 | aucun | matrice 24×24, KE0 3D (réf. Belytschko ch.9) |
| Assembly | aucun | CSR + kernel Metal (atomicAdd, LL-LIT-008) |
| SpMV | aucun | multiplication creuse GPU |
| Solveur | aucun | CG Jacobi GPU (LL-LIT-009 précision float) |
| Densité/SIMP/OC/filtre | en 2D (P1) | porter en 3D |
| Visu | aucune | marching cubes ρ=0.5 → STL |

---

## Modifications requises pour Phase 3 (quand Phase 2 sera finie)

| Composant | Phase 2 (cible finie) | Phase 3 |
|---|---|---|
| Solveur linéaire | CG + Jacobi | CG + warm-start (+ V-cycle optionnel) |
| Stratégie TO | une grille fine | hiérarchie 32³→64³→128³→256³ |
| Filtre Helmholtz | rayon en cellules | **rayon en mm** (mesh-independent) |
| Validation | convergence sur 128³ | **mesh independence** multi-résolution |

---

## Architecture cible Phase 3

```
TopOptP3/  (copie structure de TopOptP2 finie)
├── src/core/Grid3DMultiLevel.{hpp,cpp}        # hiérarchie N niveaux
├── src/fem/ProlongationOperator.{hpp,cpp}     # grossier → fin
├── src/fem/RestrictionOperator.{hpp,cpp}      # fin → grossier
├── src/filter/HelmholtzFilterPhysical.{hpp,cpp} # rayon en mm
├── src/topopt/MultiGridOptimizer.{hpp,cpp}    # loop multi-grid
├── shaders/{prolongation,restriction}.metal
└── tests/{test_prolongation,test_restriction,test_filter_physical,test_mesh_independence}.cpp
```

---

## Pièges spécifiques Phase 3 (renvois LESSONS_LEARNED)

- **LL-LIT-006** — rayon filtre en mm (pas cellules), sinon mesh independence échoue.
- **LL-LIT-010** — interpolation conservative, sinon dérive de volume entre niveaux.
- Continuation de p entre niveaux : décision à documenter (`docs/DECISIONS.md`).
- Mauvais minimum grossier propagé par warm-start : tester plusieurs init.
- V-cycle non convergent : fallback CG+Jacobi+warm-start, à documenter.

---

## Validations obligatoires fin de Phase 3

- Round-trip prolongation∘restriction : < 1e-6 (double) / 1e-4 (float)
- Conservation volume entre niveaux : < 0.01 %
- Mesh independence MBB 3D (64³ vs 128³ vs 256³, r mm fixe) : features identiques
- Speedup ≥ 5× vs Phase 2 single-grid (mesuré)
- Sensibilité au rayon (1mm vs 2mm) : variation prévisible
- Patch test FEM 3D conservé : < 1e-6 (float)

---

## Référence canonique

Aage, Andreassen, Lazarov 2015 (*Struct. Multidisc. Optim.* 51:565) ;
Lazarov, Sigmund 2011 (*Int. J. Numer. Methods Eng.* 86:765, filtre Helmholtz).
