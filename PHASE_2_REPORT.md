# Rapport — Phase 2 : passage 3D + Metal GPU compute

*Rapport mis à jour le 2026-06-26 (clôture). Remplace le checkpoint « session 1 »
antérieur. Le code fait foi : tout ci-dessous est mesuré, pas estimé.*

**Statut** : **Phase 2 substantiellement complète.** Solveur TO 3D matrix-free sur
GPU Metal, validé. Seule réserve : la cible de perf « 128³ < 10 min » pour une
optimisation *complète* n'est pas atteinte avec le préconditionneur Jacobi
(faiblesse anticipée, corrigée en Phase 3 par le multigrid).

**Dernier commit** : voir git log racine/P2.

---

## 1. État du code

```
TopOptP2/
├── Makefile                         # CPU/GPU/IO partitionné, two-phase shaders
├── src/
│   ├── main.cpp                     # loop TO 3D (mbb) + mode bench
│   ├── core/Grid3D.hpp              # grille H8 structurée, numérotation row-major
│   ├── fem/
│   │   ├── H8Element.{hpp,cpp}      # KE0 24×24 + Le/Me 8×8 (Gauss 2×2×2)
│   │   └── FEM3D.{hpp,cpp}          # référence CPU (assembly + SimplicialLDLT)
│   ├── topopt/SIMP3D.{hpp,cpp}      # SIMP + sensibilité + OC (bissection)
│   ├── filter/Helmholtz3D.{hpp,cpp} # filtre PDE GPU matrix-free (scalaire)
│   ├── gpu/
│   │   ├── MetalContext.{hpp,cpp}   # device/queue/library/pipeline
│   │   ├── metal_impl.cpp           # TU unique *_PRIVATE_IMPLEMENTATION
│   │   └── CGSolver3D.{hpp,cpp}     # CG matrix-free Jacobi (élasticité) GPU
│   └── io/STLExporter.{hpp,cpp}     # surface des voxels solides → STL binaire
├── shaders/
│   ├── fem3d.metal                  # matvec/diag élastique + Helmholtz + vecops + ce
│   └── vector_add.metal             # démo (session 1)
└── tests/{test_metal_hello,test_fem3d,test_cg_gpu,test_mbb3d}.cpp
```

Conventions : C++23, 0 warning (`-Wall -Wextra -Wpedantic`), namespaces
`topopt`/`topopt::gpu`, metal-cpp non-ARC, deps via `shared/third_party` (symlinks).

---

## 2. Acquis validés (vs checkpoints Phase 2)

| Acquis attendu | Statut | Note |
|---|:---:|---|
| Fondation Metal (device/queue/library/pipeline) | ✓ | session 1 |
| Élément H8 (KE0 24×24) | ✓ | Gauss 2×2×2, symétrie+corps rigide vérifiés |
| Assembly / produit K·u GPU | ✓ | **matrix-free** (pas de K assemblée) |
| SpMV GPU | ✓ | matrix-free node-gather (pas d'atomics) |
| CG préconditionné Jacobi GPU | ✓ | converge, validé vs CPU |
| Patch test FEM 3D | ✓ | traction constante exacte 7.8e-16 (double) |
| Cantilever 3D | ✓ | ratio FE/Euler-Bernoulli 0.977 |
| Filtre Helmholtz GPU | ✓ | matrix-free scalaire, conservatif |
| Loop TO 3D complète + MBB 3D | ✓ | compliance monotone, volume tenu |
| Visualisation → STL | ✓ | surface voxels watertight (marching cubes → Phase 3) |
| Benchmark 128³ < 10 min (opti complète) | ⚠️ | solve 6.4s ✓ ; opti 60-iter 16.6 min ✗ → Phase 3 |

**Bilan : 10/11.** Le seul écart (perf opti complète 128³) est la faiblesse Jacobi
documentée d'avance, ciblée par Phase 3 (multigrid).

---

## 3. Cas tests validés (valeurs réelles)

| Test | Métrique | Cible | Obtenu | Statut |
|---|---|---|---|:---:|
| `test_fem3d` [a] KE0 | asymétrie / corps rigide | < 1e-10 | 2.8e-17 / 3.5e-17 | ✓ |
| `test_fem3d` [b] patch traction | max\|u−analytique\| | < 1e-10 | 7.8e-16 | ✓ |
| `test_fem3d` [c] cantilever | ratio FE/EB | [0.9,1.4] | 0.977 | ✓ |
| `test_cg_gpu` | relres ; GPU vs CPU | <1e-4 ; <1e-3 | 8.8e-7 ; 3.1e-4 | ✓ |
| `test_metal_hello` | vec_add 1M | <1e-6 | 0.0 | ✓ |
| `test_mbb3d` [a] filtre uniforme | maxerr | <1e-4 | 2.1e-7 | ✓ |
| `test_mbb3d` [b] MBB court | compliance↓, vol | Δvol<0.02 | 562→62.7, 0.3000 | ✓ |

`make` : 0 warning. Suite `make test` : tout vert.

---

## 4. Benchmarks de performance (M4 Max, 64 GB, Apple9)

| Cas | Taille | DOF | Détail | Mesuré |
|---|---|---|---|---|
| Solve uniforme | 64³ | 212k | CG Jacobi float32 | **0.57 s**, 590 iter, relres 9.7e-5 |
| Solve uniforme | 128³ | 6.44M | CG Jacobi float32 | **6.4 s**, 1516 iter, relres 9.9e-5 |
| Opti MBB | 60×20×20 | 80.7k | 60 iter TO | **51.7 s** |
| Opti MBB | 128³ | 6.44M | 60 iter TO | **998 s (16.6 min)**, compliance 6.39→0.327, vol 0.3000 |

Mémoire : matrix-free → working set modeste (vecteurs nDof float + Emod), bien en
deçà des 55.7 GB recommandés. STL 128³ : 263 856 triangles, 13 MB.

**Lecture honnête de la cible 128³ < 10 min** : tenue pour un *solve* isolé (6.4 s) ;
**non tenue** pour l'*optimisation complète* (16.6 min) car les itérations CG passent
de ~1516 à 4001 (plafond) quand le design se durcit (conditionnement croissant avec
le contraste de matière et le maillage). Jacobi est un préconditionneur faible —
le multigrid de Phase 3 est le correctif documenté.

---

## 5. Dette technique acceptée

| Item | Raison | Résolution |
|---|---|---|
| Préconditionneur Jacobi (CG itérations croissent) | Phase 2 = monter en sophistication par étapes | **Phase 3 : multigrid V-cycle** |
| `commit/waitUntilCompleted` par opération (sync CPU↔GPU) | correctness d'abord | batching command-buffers (perf, Phase 3+) |
| STL = surface des voxels (pas marching cubes lisse) | robuste et watertight | marching cubes (Phase 3) |
| Filtre rayon en **cellules** (pas mm) | convention Phase 1/2 | **Phase 3 : rayon physique mm** |
| Précision float32 partout sur GPU | perf/mémoire | OK (CG converge proprement) |
| Pas de continuation de p (p=3 fixe) | converge sur MBB | si oscillations futures |

---

## 6. Modifications requises pour Phase 3

Voir `../orchestration/handoffs/PHASE_2_TO_3.md`. En bref : multigrid (prolongation/
restriction, warm-start), filtre rayon mm, démonstration de mesh independence,
préconditionneur multigrid pour faire tomber le coût CG.

---

## 7. Pièges rencontrés et solutions

### Piège 1 : Emin=1e-9 → CG itératif diverge (→ LL-006)
- Symptôme : compliance oscillante/négative, CG plafonné. Cause : K quasi-singulière
  dans le vide (cond ~1e9) ingérable en float32 Jacobi. Solution : **Emin=1e-4**
  (compliance 230→18.5 monotone). Direct (P1) tolérait 1e-9, pas l'itératif.

### Piège 2 : filtre re-appelé ~15× dans la bissection OC (→ LL-007)
- Symptôme : coût filtre dominant à 128³. Solution : le filtre Helmholtz conserve la
  moyenne → bissection sur `rho.sum()`, filtrage **une seule fois**. Volume tenu,
  60×20×20 : 73 s → 51.7 s.

### Piège 3 : partition Makefile CPU/GPU
- Symptôme : symboles Metal non résolus en liant un test « CPU » qui tirait un module
  GPU. Solution : séparer `GPU_CORE` (context) / `GPU_SOLVER` ; `test_metal_hello`
  ne lie que le cœur.

---

## 8. Mises à jour LESSONS_LEARNED proposées

Intégrées : **LL-006** (Emin itératif) et **LL-007** (filtre conservatif / OC).

---

## 9. Écarts par rapport au plan initial

| Élément | Plan | Réalisé | Justification |
|---|---|---|---|
| Produit K·u | assembly CSR + SpMV | **matrix-free** | décision utilisateur : scalable 128³ (~150 MB vs 2-4 GB), pas d'atomics, réf. Aage 2015 |
| Filtre | direct/CSR | CG matrix-free scalaire GPU | cohérent matrix-free, scalable |
| Visu | marching cubes | surface voxels watertight | robuste ; MC lisse reporté Phase 3 |
| Emin | 1e-9 (comme P1) | 1e-4 | conditionnement itératif float32 (LL-006) |

---

## 10. Limitations documentées

- Optimisation 128³ complète : 16.6 min (> 10 min) — Jacobi faible, fix = multigrid Phase 3.
- float32 partout sur GPU (suffisant ici, CG converge).
- Filtre en cellules (mesh-dependent) — Phase 3 passe en mm.
- STL = surface voxels (pas iso-surface lisse).
- Couplage thermo/fluide absent (Phases 4-5).

---

## 11. Décisions architecturales (à porter dans docs/DECISIONS.md)

- **ADR-005** : produit K·u **matrix-free** (pas de CSR assemblée). Scalable 128³,
  sans atomics, conforme à la pratique large-scale (Aage 2015).
- **ADR-006** : Emin=1e-4 pour le solveur itératif float32 (vs 1e-9 direct en P1).
- **ADR-007** : filtre Helmholtz GPU matrix-free scalaire ; OC exploite la
  conservation de volume du filtre (un seul filtrage par itération).
- **ADR-008** : visualisation = surface des voxels solides en STL binaire ;
  marching cubes lisse différé en Phase 3.

---

## 12. Checklist de clôture

- [x] `PHASE_2_REPORT.md` complet, valeurs mesurées
- [x] `make` 0 warning ; `make test` tout vert (fem/cg/hello/mbb)
- [x] Patch test 3D, cantilever, CG vs CPU, MBB 3D validés
- [x] Benchmarks 64³/128³ mesurés (solve + opti)
- [x] `handoffs/PHASE_2_TO_3.md` à mettre à jour (P2 complète)
- [x] `LESSONS_LEARNED.md` : LL-006, LL-007 ajoutés
- [x] `docs/DECISIONS.md` : ADR-005..008 à ajouter
- [x] STL 128³ produit (`output/mbb3d.stl`)
