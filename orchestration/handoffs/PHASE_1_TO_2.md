# Handoff PHASE 1 → PHASE 2

*Reconstruit rétroactivement le 2026-06-26 à partir du code réel
(`analysis/CODE_ANALYSIS.md`), non d'un planning théorique. Le code fait foi.*

Remplace l'ancien `TopOptP1/PHASE_1_TO_PHASE_2_HANDOFF.md` (archivé dans
`analysis/_legacy/TopOptP1/`).

---

## État du code en sortie de Phase 1

**Phase 1 terminée et validée** (`make test` → ALL PASS). ~716 LOC, C++23,
solveur CPU direct.

Arborescence réelle (hors third_party/build) :
```
TopOptP1/
├── Makefile                       # clang++ C++23, -O3, -Wall -Wextra -Wpedantic
├── assets/problem_mbb.json        # paramètres MBB
├── src/
│   ├── main.cpp                   # loop TO + I/O JSON inline + BCs MBB
│   ├── core/Grid2D.{hpp,cpp}      # grille Q4, numérotation top88 column-major
│   ├── fem/FEM2D.{hpp,cpp}        # élasticité Q4 plane stress, solve direct LDLT
│   ├── topopt/SIMP.{hpp,cpp}      # SIMP + sensibilité compliance + OC bissection
│   ├── filter/Helmholtz.{hpp,cpp} # filtre densité PDE, rayon en cellules
│   └── io/PNGWriter.{hpp,cpp}     # export PNG (stb)
└── tests/test_mbb_beam.cpp        # 3 tests : traction, filtre, MBB court
```

### Faits techniques à transmettre (vérité du code)
- **Solveur** : `Eigen::SimplicialLDLT` (direct, **LDLᵀ**, pas LLT).
- **SIMP** : `E = Emin + ρ^p (E0−Emin)`, **Emin = 1e-9**, **ρ ∈ [0,1]** (clamp OC).
  Le "ρ_min" est réalisé par Emin, pas par un plancher sur ρ.
- **OC** : bissection du multiplicateur (l1=0, l2=1e9, tol 1e-3), move=0.2, exp 0.5.
- **Filtre Helmholtz** : `(−r²∇²+1)ρ̃=ρ`, rayon **en cellules**, `r=r_cells/(2√3)`.
- **Grid2D** : numérotation **top88 column-major** `node_id=col*(nely+1)+row`,
  row 0 = haut ; ordre nœuds `[bl,br,tr,tl]`, DOF top88.
- **BCs** : réduction aux DOF libres (pas de zero-out row/col explicite).
- **p fixé = 3** (pas de continuation 1→2→3).
- Conventions : 4 espaces, namespace `topopt`, PascalCase/camelCase, membres `_`.

### Cas tests validés (valeurs réelles)
| Test | Cible | Obtenu |
|---|---|---|
| Traction analytique δ=FL/(EA), 12×3 | < 1e-9 | 4.8e-14 ✓ |
| Filtre uniforme préservé, 40×20 | < 1e-10 | 3.3e-16 ✓ |
| MBB court 60×20, 25 iter | compliance ↓, Δvol<0.01 | 1007→242, vol 0.5000 ✓ |

Cas applicatif `make run` : MBB 200×100, vol 0.5, p=3, R=2, 100 iter.

---

## Modifications requises pour Phase 2

| Composant | Phase 1 | Phase 2 (à coder) |
|---|---|---|
| Dimension | 2D Q4 | 3D hexaèdres H8 (24×24, 3 DOF/nœud) |
| Indexation | (i,j) top88 column-major | (i,j,k) row-major + Morton optionnel |
| Assembly | loop CPU, triplets Eigen | kernel Metal, CSR custom (atomicAdd) |
| Solveur | direct LDLT | CG préconditionné Jacobi sur GPU |
| Format K | `Eigen::SparseMatrix<double>` | `MTLBuffer` CSR (values, col_idx, row_ptr) |
| Précision | double | float sur GPU |
| Filtre | direct Eigen | CG GPU même format |
| Visu | PNG 2D | marching cubes → STL |
| Taille | 200×100 ≈ 20k DOF | 128³ ≈ 6M DOF (×3) |

---

## Pièges spécifiques Phase 2

- **LL-LIT-008** — race conditions assembly GPU : nœuds partagés → atomicAdd ou
  graph-coloring, sinon K mal assemblée silencieusement.
- **LL-LIT-009** — précision float32 CG : résidu peut stagner sur bruit numérique ;
  comparer à double sur petit cas.
- **LL-002** — ne pas se fier aux noms de fichiers de référence ; vérifier le code.
- Coalescence mémoire (ordre Morton/row-major) ; tailles threadgroups (256/512) ;
  memory pressure à 128³ ; `newLibrary(path)` en CLI ; metal-cpp non-ARC.

---

## Validations obligatoires fin de Phase 2

- Patch test FEM 3D : < 1e-6 (float) / 1e-10 (double)
- Cantilever 3D analytique : flèche < 2 %
- MBB 3D : design symétrique cohérent, compliance dans la fourchette Aage 2015
- CG : résidu/résidu₀ < 1e-6 en < 2000 iter sur cas test
- Benchmark 128³ < 10 min wall-clock
- STL manifold, surface fermée, ouvrable dans MeshLab

---

## Référence canonique

Aage, Lazarov, Sigmund 2015, *Struct. Multidisc. Optim.* 51:565 (large-scale parallel TO).
