# Analyse archéologique du code TopOpt

*Produit le 2026-06-26. Source de vérité = le code qui compile et dont les
tests passent. Les documents (CLAUDE.md, TRANSITIONS.md, handoffs) sont des
intentions ; en cas de conflit, **le code fait foi**.*

Git baseline posée avant analyse :
- `TopOptP1` : commit `c4529db` "Phase 1 baseline — pre-reset snapshot"
- `TopOptP2` : commit `eaf3939` "Phase 2 baseline — pre-reset snapshot"

---

## Phase 1 — TO structurelle 2D SIMP

### Arborescence réelle (code, hors third_party/build)
```
TopOptP1/
├── Makefile                     # clang++ C++23, -O3, -Wall -Wextra -Wpedantic
├── assets/problem_mbb.json      # paramètres du cas MBB
├── src/
│   ├── main.cpp                 # 139 L — loop TO complète + I/O JSON inline + BCs MBB
│   ├── core/Grid2D.{hpp,cpp}    # 45+23 L — grille structurée Q4, numérotation top88
│   ├── fem/FEM2D.{hpp,cpp}      # 45+104 L — élasticité Q4 plane stress, solve direct
│   ├── topopt/SIMP.{hpp,cpp}    # 51+46 L — SIMP + sensibilité compliance + OC
│   ├── filter/Helmholtz.{hpp,cpp} # 34+71 L — filtre densité PDE
│   └── io/PNGWriter.{hpp,cpp}   # 18+26 L — export PNG (stb)
└── tests/test_mbb_beam.cpp      # 114 L — 3 tests en un fichier
```
**Total ≈ 716 LOC.** Build OK, `make test` → **ALL PASS**.

### Modules et rôles réels
| Module | Rôle réel |
|---|---|
| `Grid2D` | Maillage Q4 structuré. Numérotation **column-major top88** : `node_id = col*(nely+1)+row`, row 0 = haut. DOF : x→2n, y→2n+1. Ordre nœuds élément `[bl,br,tr,tl]`, ordre DOF top88 `[bl_x,bl_y,br_x,br_y,tr_x,tr_y,tl_x,tl_y]`. |
| `FEM2D` | KE0 analytique top88 (E=1, plane stress, fct de ν). Assemblage réduit sur DOF libres (Dirichlet homogène par réduction, pas de zero-out row/col). **Solveur direct `Eigen::SimplicialLDLT`**. `elementStrainEnergy` = uₑᵀ KE0 uₑ. |
| `SIMP` | `E = Emin + ρ^p (E0−Emin)`. Sensibilité compliance `dc = −p (E0−Emin) ρ^(p−1) cₑ`. **OC par bissection** du multiplicateur (l1=0, l2=1e9, tol 1e-3), move=0.2, exposant 0.5. Filtre injecté en `std::function` (découplage). |
| `Helmholtz` | Filtre PDE `(−r²∇²+1)ρ̃=ρ` résolu par SimplicialLDLT (membre `solver_`). **Rayon en cellules**, conversion `r = radiusCells/(2√3)`. Map élément→nœud (¼ par nœud) puis retour nœud→élément (moyenne). |
| `PNGWriter` | Export PNG du champ densité via stb. |
| `main` | Charge `assets/problem_mbb.json` (nlohmann **inline**, pas de module dédié), définit les BCs MBB, lance la loop, écrit PNG toutes les 10 itérations. |

### Conventions de code observées (le code, pas les docs)
- **C++23**, `clang++`, `-O3 -DEIGEN_NO_DEBUG -DNDEBUG`, `-Wall -Wextra -Wpedantic` (0 warning).
- **Indentation : 4 espaces** (≠ "2 espaces" annoncés dans CLAUDE.md → voir divergences).
- `namespace topopt` ; classes **PascalCase**, méthodes **camelCase**, membres suffixe `_`.
- `#pragma once`, RAII (pas de new/delete), `const`-correctness systématique.
- Eigen : `VectorXd`, `SparseMatrix<double>`, triplets. Dépendances en `-isystem`.
- Pas de `[[nodiscard]]` observé (annoncé dans CLAUDE.md mais non appliqué).

### Cas tests identifiés (paramètres réels)
| Test (dans `test_mbb_beam.cpp`) | Nature | Tolérance | Résultat mesuré |
|---|---|---|---|
| `testTensionBar` | Barre en traction vs analytique δ=FL/(EA), maillage 12×3 | < 1e-9 | **4.8e-14** ✓ |
| `testFilterUniform` | Filtre Helmholtz préserve champ uniforme, 40×20 | < 1e-10 | **3.3e-16** ✓ |
| `testMbbShort` | MBB 60×20, 25 iter : compliance ↓ ET volume tenu | Δvol < 0.01 | c1=1007→cN=242, vol=0.5000 ✓ |

Cas applicatif principal (`make run`) : **MBB 200×100**, vol=0.5, p=3, R=2, 100 iter.
BCs MBB (demi-domaine par symétrie) : bord gauche ux=0, coin bas-droit uy=0,
charge unitaire verticale vers le bas au coin haut-gauche.

### État de validation observable
- Build propre, tests verts. Validation analytique (traction) + filtre + descente
  de compliance avec conservation de volume : **Phase 1 fonctionnellement validée**.

---

## Phase 2 — Fondation Metal GPU

### Arborescence réelle
```
TopOptP2/
├── Makefile                       # two-phase : .metal→.metallib puis C++/link
├── src/gpu/
│   ├── MetalContext.{hpp,cpp}     # 51+79 L — device, queue, library, pipeline
│   └── metal_impl.cpp             # 9 L — TU unique *_PRIVATE_IMPLEMENTATION
├── shaders/vector_add.metal       # 12 L — kernel démo
└── tests/test_metal_hello.cpp     # 92 L — vec_add 1M GPU vs CPU
```
**Total ≈ 243 LOC.** Build OK, `make test` → match GPU/CPU exact.

### Modules ajoutés vs Phase 1
- `topopt::gpu::MetalContext` : possède device + queue + library, fabrique des
  pipelines compute. **Ref counting manuel** (metal-cpp non-ARC), forward decls
  MTL:: pour garder les headers lourds hors du .hpp.
- `metal_impl.cpp` : seule TU avec `*_PRIVATE_IMPLEMENTATION`.
- Aucune réutilisation du code Phase 1 (P2 démarre vierge, cf. ADR-001).

### Architecture GPU réellement implémentée
- **Uniquement** : init device, chargement `.metallib`, pipeline compute, dispatch
  d'un kernel trivial (`vec_add` sur 1M floats), comparaison GPU vs CPU.
- **Absents** : élément H8, assembly CSR, SpMV, CG, préconditionneur, filtre GPU,
  marching cubes/STL, tout solveur 3D.

### Cas tests identifiés
| Test | Métrique | Résultat |
|---|---|---|
| `test_metal_hello` | `vec_add` 1 048 576 floats, StorageModeShared | max\|gpu−cpu\|=0.000e+00 ✓ |
| Détection device | nom/family/working set | M4 Max / Apple9 / 55.66 GB ✓ |

### État de validation observable
- **Phase 2 = fondation seule (≈1/9 des checkpoints).** Conforme à
  `PHASE_2_REPORT.md` (avertissement d'honnêteté présent). **Phase 2 n'est PAS
  substantiellement terminée** : aucun solveur FEM 3D GPU n'existe.

---

## Divergences avec le planning théorique

Référence : `TopOptP1/TRANSITIONS.md`, `TopOptP1/CLAUDE.md`,
`TopOptP1/PHASE_1_TO_PHASE_2_HANDOFF.md`.

| # | Docs annoncent | Code réel | Gravité |
|---|---|---|---|
| D1 | Solveur `Eigen::SimplicialLLT` (Cholesky) | `Eigen::SimplicialLDLT` (LDLᵀ) | Faible (doc à corriger) |
| D2 | `ρ ∈ [ρ_min, 1]`, ρ_min = 1e-3 "jamais zéro strict" | ρ ∈ **[0, 1]** (clamp 0..1 dans OC), plancher de rigidité via **Emin = 1e-9** | Moyenne (concept mal décrit) |
| D3 | Continuation de p : 1→2→3 sur ~30 iter | **p fixé = 3.0**, aucune continuation | Moyenne (feature non implémentée) |
| D4 | Indentation 2 espaces | **4 espaces** | Faible (convention à acter) |
| D5 | `[[nodiscard]]` sur retours non-triviaux | **Non appliqué** | Faible |
| D6 | Modules séparés : Mesh2D, ElementMatrix, Assembly, LinearSolver, ObjectiveCompliance, OptimalityCriteria, JSONLoader, benchmarks/{MBB,Cantilever} | Modules **fusionnés** : FEM2D (élément+assembly+solveur), SIMP (objectif+OC), JSON inline dans main, pas de Mesh2D ni dossier benchmarks | Moyenne (archi réelle plus compacte) |
| D7 | Tests : `test_patch.cpp`, `test_cantilever.cpp`, `test_mbb.cpp` séparés ; "test patch champ uniforme à 1e-12" | **1 fichier** `test_mbb_beam.cpp` (traction + filtre + MBB court). **Pas de patch test**, pas de test cantilever de flèche | Moyenne |
| D8 | Conventions FEM : "zero-out row AND column" pour les BCs | BCs par **réduction aux DOF libres** (équivalent numériquement, mécanisme différent) | Faible (description inexacte) |
| D9 | `PHASE_2_INITIAL_PROMPT.md` rangé dans `TopOptP1/` | Placement incohérent (prompt P2 sous P1) | Faible (rangement) |

**Note** : ces divergences ne sont **pas des bugs** — le code est cohérent et
validé. Ce sont des écarts **documentation↔implémentation** à réconcilier.

---

## Conventions de code consolidées (à reprendre dans MASTER_CLAUDE.md)

À acter comme **vérité** (issues du code) :
- C++23, clang++, Makefile, `-O3 -DEIGEN_NO_DEBUG -DNDEBUG`, `-Wall -Wextra -Wpedantic`, 0 warning.
- **Indentation 4 espaces** (et non 2). PascalCase classes, camelCase méthodes, membres `_`.
- `namespace topopt` (CPU), `namespace topopt::gpu` (Metal).
- `#pragma once`, RAII, const-correctness. `[[nodiscard]]` : **optionnel** (non systématique aujourd'hui).
- Dépendances vendorisées via `shared/third_party/` (symlinks depuis chaque phase), en `-isystem`.
- Metal : metal-cpp **non-ARC**, ref counting manuel, `*_PRIVATE_IMPLEMENTATION` dans une TU unique, forward decls MTL:: dans les headers, `newLibrary(path)` (pas `newDefaultLibrary`), `StorageModeShared`.
- FEM : numérotation **top88 column-major**, KE0 analytique, solveur direct LDLᵀ en 2D.
- TO : SIMP `E=Emin+ρ^p(E0−Emin)`, OC par bissection (move=0.2, exp 0.5), filtre Helmholtz PDE (rayon en cellules en P1).

---

## Dette technique observable

- **Phase 2 inachevée** : tout le solveur 3D GPU reste à écrire (dette la plus lourde).
- Pas de continuation de p (peut faire osciller OC sur cas difficiles ; OK sur MBB).
- ρ_min concept : le code utilise Emin (plancher rigidité) et non un plancher sur ρ ;
  acceptable mais à clarifier avant le 3D (singularité K en zones vides).
- Pas de **patch test FEM** vrai (champ uniforme) — la méta-règle "patch test obligatoire"
  de TRANSITIONS.md n'est pas satisfaite stricto sensu (le test de traction en tient lieu).
- I/O JSON inline dans `main` (P1) : pas réutilisable tel quel pour d'autres cas.
- Doublon `metal-cpp/` (racine, 1.8M, avec MetalFX+SingleHeader) vs
  `shared/third_party/metal-cpp/` (1.6M, sous-ensemble utilisé par P2). Le P2 pointe
  vers shared ; le metal-cpp racine est **orphelin** (plus complet).
- Visualisation : PNG 2D seulement (P1) ; rien en 3D.

---

## Bugs ou incohérences détectés (inspection, SANS correction)

Aucun **bug fonctionnel** détecté à l'inspection (tests verts, descente de
compliance correcte, conservation de volume tenue). Points d'attention :

1. **Pas de garde-fou sur ρ→0 et rigidité** : en 2D avec Emin=1e-9 c'est stable
   (LDLᵀ direct), mais en 3D itératif (CG) ce plancher très bas pourrait dégrader
   le conditionnement. À surveiller en Phase 2/3 (non corrigé ici).
2. **OC : `Be(e)` suppose `dv > 0`** ; `dv0 = filter(1)` l'est toujours, donc OK.
   Pas de division par zéro possible avec l'usage actuel.
3. **`metal-cpp` racine orphelin** : risque de confusion sur "quelle copie fait foi".
   Décision de rangement à prendre en Étape 6 (pas une correction de code).

→ **Aucune modification de code appliquée.** Toutes ces remarques sont reportées
pour décision ultérieure de l'utilisateur.
