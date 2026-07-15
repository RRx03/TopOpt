# TECHNICAL.md — TopOpt : comment ça marche, informatiquement

> Document technique de référence. Le code fait foi : chaque affirmation cite
> le(s) fichier(s) source. Complète `docs/ARCHITECTURE.md` (qui date de la
> Phase 2 et ne décrit que la fondation GPU : MetalContext, `metal_impl.cpp`,
> flux de build `.metal → .air → .metallib`) sans le dupliquer. Pour la théorie
> (équations, adjoints), voir `docs/THEORY.md` ; pour le langage d'entrée,
> `docs/INPUT_LANGUAGE.md` ; pour les choix d'architecture, `docs/DECISIONS.md`.

---

## 1. Vue d'ensemble

Le dépôt est un monorepo à **trois étages** :

1. **Le solveur C++** (`src/`, `shaders/`, `Makefile`) — binaires natifs macOS
   Apple Silicon. Deux familles : le chemin structurel 3D GPU (Metal,
   matrix-free, float32) et le chemin multiphysique CPU (Eigen, double
   précision, solveurs directs).
2. **Le contrat `.topopt.json`** — un fichier JSON décrit intégralement un
   problème (domaine, matériau, physiques, BC, filtre, optimisation, sorties).
   Parsé côté C++ par `src/io/ProblemSpec.hpp`, produit côté web par
   `web/src/spec/serialize.ts`. C'est l'interface unique entre les deux mondes.
3. **Le Studio web + serveur de run** — `web/` (Vite/TypeScript strict,
   three.js, vtk.js) pour éditer le problème et visualiser les résultats ;
   `server/run-server.mjs` (Node natif, zéro dépendance npm) pour lancer
   `build/topopt_run` sur la machine de calcul et streamer les logs en SSE.

```
Studio web (web/)                serveur de run (server/)         solveur (build/)
  éditeur three.js  --spec-->    POST /api/run          --spawn-->  topopt_run spec.json
  viewer vtk.js     <--SSE---    GET /api/jobs/:id/log  <--pty----  stdout ligne à ligne
                    <--HTTP--    GET .../artifacts/f    <--------   output/<name>-<ts>/*.vti|.stl|.png
```

### 1.1 Dépendances vendorisées (`third_party/`)

Aucun gestionnaire de paquets côté C++ : tout est vendorisé, le build ne
dépend que de Xcode (clang + Metal toolchain) et de `make`.

| Lib | Version | Rôle | Preuve |
|---|---|---|---|
| **Eigen** | 3.4.0 | Algèbre linéaire dense + sparse (SimplicialLDLT, SparseLU), tout le chemin CPU | `third_party/eigen/Eigen/src/Core/util/Macros.h` (EIGEN_WORLD/MAJOR/MINOR_VERSION = 3/4/0) |
| **metal-cpp** | macOS 26 / iOS 26 | Bindings C++ header-only de l'API Metal (pas d'Objective-C dans le projet) | `third_party/metal-cpp/README.md` |
| **nlohmann/json** | 3.11.3 | Parsing du `.topopt.json` | `third_party/nlohmann/json.hpp` (NLOHMANN_JSON_VERSION_* = 3/11/3), inclus via `#include <nlohmann/json.hpp>` dans `src/io/ProblemSpec.hpp:7` |

Header-only pour les trois : rien à compiler dans `third_party/`.

### 1.2 Build (`Makefile`)

- **Compilateur** : `clang++ -std=c++23 -Wall -Wextra -Wpedantic -O3
  -DEIGEN_NO_DEBUG -DNDEBUG` (`Makefile:4-7`).
- **`-isystem` pour les deps** : notre code est compilé avec les warnings
  complets (`-Isrc`), les deps vendorisées avec `-isystem third_party/...`
  pour ne pas noyer le build sous leurs warnings (`Makefile:8-11`).
- **`-fno-objc-arc` obligatoire** : metal-cpp fait son propre comptage de
  références manuel ; l'ARC du compilateur entrerait en conflit
  (`Makefile:9,12`).
- **Link** : `-framework Metal -framework Foundation -framework QuartzCore`
  uniquement pour les binaires GPU (`Makefile:13`) ; les binaires CPU-purs
  (adjoints, Stokes, CHT, axi…) sont linkés sans frameworks
  (ex. `Makefile:113-114`).
- **Shaders compilés séparément** : `.metal → .air` par `xcrun -sdk macosx
  metal -c`, puis tous les `.air` fusionnés en un unique
  `build/shaders.metallib` par `xcrun metallib` (`Makefile:279-284`). Le
  metallib est chargé à l'exécution (pas embarqué dans le binaire).
- **Un seul TU metal-cpp** : `src/gpu/metal_impl.cpp` est le seul fichier
  définissant `NS/CA/MTL_PRIVATE_IMPLEMENTATION` (cf. `docs/ARCHITECTURE.md`),
  sinon symboles dupliqués au link.
- **Cibles principales** : `make all` (tout), `make test` (suite complète GPU
  incluse, 22 binaires exécutés, `Makefile:222-244`), `make test_cpu` (17
  binaires CPU-purs, sans metallib ni GPU, `Makefile:246-264`), `make run`
  (`./build/topopt mbb`, `Makefile:266-267`).
- **Deux drivers** : `build/topopt` (= `src/main.cpp`, modes CLI `mbb` /
  `bench` / `mg`, `src/main.cpp:123-135`) et `build/topopt_run`
  (= `src/apps/topopt_run.cpp`, driver JSON : `topopt_run <problem.topopt.json>`,
  `src/apps/topopt_run.cpp:1081-1088`). `topopt_run` linke TOUT (GPU + tous
  les adjoints CPU + axi + IO, `Makefile:209-210`).

`setup.sh` automatise l'installation : vérifie macOS/Apple Silicon, clang,
make, node ≥ 20 ; `npm install` dans `web/` ; télécharge le Metal Toolchain
via `xcodebuild` s'il manque (non bloquant pour le Studio) ; puis `make -j`
et, avec `--test`, `make test_cpu` (`setup.sh:22-75`).

---

## 2. Le GPU : chemin structurel 3D (v1)

### 2.1 Matrix-free élément-par-élément

La grille est structurée, à cellules H8 (hexaèdres trilinéaires) unitaires
identiques (`src/core/Grid3D.hpp`). Conséquence exploitée partout : **la
matrice de raideur élémentaire E=1, `KE0` (24×24), est la même pour tous les
éléments**. La raideur globale n'est **jamais assemblée** ; le produit
`y = K·u` est recalculé à la volée :

- `KE0` est calculée une fois sur CPU en double (quadrature de Gauss 2×2×2,
  points ±1/√3, `src/fem/H8Element.cpp:75-117`), convertie en `float[576]`
  row-major dans un constant buffer Metal (`src/gpu/CGSolver3D.cpp:47-51`).
- Le kernel `mf_matvec_elastic` lance **1 thread par nœud** : chaque thread
  accumule les contributions des (≤ 8) éléments incidents en pondérant `KE0`
  par le module de Young de l'élément (`shaders/fem3d.metal:20-76`). Aucune
  opération atomique, aucun coloring : chaque thread n'écrit que ses 3 DOF.
- Coût mémoire : quelques vecteurs de taille nDof + un `float` par élément
  (E), soit **O(n)**, contre O(nnz) ≈ 81×nDof en double pour un assemblage
  CSR 3D — c'est ce qui rend les grandes grilles possibles sur un GPU intégré
  à mémoire unifiée.

### 2.2 Kernels Metal

Tous les dispatchs utilisent des threadgroups de **256 threads** (multiple de
la largeur SIMD 32 d'Apple Silicon, cf. `docs/ARCHITECTURE.md`, threading
model), 1 thread par nœud, par DOF ou par élément selon le kernel.

`shaders/fem3d.metal` (élasticité + filtre + BLAS1) :

| Kernel | Ligne | Rôle |
|---|---|---|
| `mf_matvec_elastic` | 20 | y = K(E)·u matrix-free, 1 thread/nœud |
| `mf_diag_elastic` | 78 | diagonale de K (préconditionneur Jacobi) |
| `zero_fixed` | 118 | projette les DOF fixés à zéro (masque uint8) |
| `precond_jacobi` | 127 | z = M⁻¹r ; z=0 sur les DOF fixés |
| `vec_axpy` / `vec_xpby` / `vec_copy` | 138/148/157 | BLAS1 |
| `vec_dot_partial` | 166 | produit scalaire : réduction intra-threadgroup (scratch 256), 1 partiel/threadgroup |
| `helm_scatter` / `helm_gather` | 191/216 | passage éléments↔nœuds pour le filtre Helmholtz |
| `mf_matvec_helmholtz` / `mf_diag_helmholtz` | 236/274 | opérateur scalaire 8×8 `KF = rlen²·Le + Me` |
| `mf_strain_energy` | 303 | énergie de déformation par élément `ce = ueᵀ·KE0·ue` (sensibilités SIMP) |

`shaders/thermal.metal` : `mf_matvec_thermal` (l.19) et `mf_diag_thermal`
(l.60) — même schéma pour la conduction (1 DOF/nœud), utilisés par
`src/physics/ThermalSolver.cpp`. `shaders/vector_add.metal` : `vec_add`,
kernel de démonstration de la Phase 2 (testé par `test_metal_hello`).

Paramètres passés par `constant GridDims& {nelx, nely, nelz, nnodes}`
(`shaders/fem3d.metal:9-11`) ; les buffers sont indexés explicitement
(`[[buffer(N)]]`).

### 2.3 CG préconditionné Jacobi (`src/gpu/CGSolver3D.{hpp,cpp}`)

PCG classique entièrement sur GPU sauf les scalaires :

- **Jacobi** : diagonale accumulée par `mf_diag_elastic`, inversée sur CPU
  (`inv[i] = fixed[i] || d==0 ? 0 : 1/d`, `CGSolver3D.cpp:220-223`).
- **DOF fixés** : jamais éliminés — masqués. `zero_fixed` après chaque matvec
  + `precond_jacobi` qui force z=0 (`fem3d.metal:118-135`).
- **Réductions** : `vec_dot_partial` produit 1 partiel float par threadgroup ;
  la somme finale est faite **sur CPU en double** (`CGSolver3D.cpp:185-187`)
  — c'est là que la précision du CG se joue.
- **Convergence** : résidu relatif `‖r‖/‖r₀‖ < tol`, défauts `tol=1e-4f`,
  `maxiter=4000` (`src/main.cpp:36-37`, `CGSolver3D.cpp:249-250`).
- **MetalContext** (`src/gpu/MetalContext.{hpp,cpp}`) : possède device/queue,
  charge `build/shaders.metallib` (`MetalContext.cpp:43-58`), fabrique les
  `MTL::ComputePipelineState` (`makePipeline`, l.60-77). Buffers en
  `StorageModeShared` (mémoire unifiée : pas de copie host↔device).

### 2.4 Filtre Helmholtz GPU (`src/filter/Helmholtz3D.{hpp,cpp}`)

Filtre de densité PDE (Lazarov-Sigmund) : `(-rlen²∇² + 1)ρ̃ = ρ` avec
`rlen = radiusCells / (2√3)` (`Helmholtz3D.cpp:48`). Résolu par le même PCG
Jacobi matrix-free, sur le champ scalaire nodal : `helm_scatter` (éléments →
RHS nodal), CG sur `KF = rlen²·Le + Me` (matrices 8×8 de `H8Element::diffusion()`
et `::mass()`), puis `helm_gather` (nœuds → éléments). Un seul filtrage par
itération d'optimisation (leçon LL-007, `archive/orchestration/LESSONS_LEARNED.md`).

### 2.5 Multigrid warm-start (`src/topopt/MultiGridOptimizer.{hpp,cpp}`, `GridTransfer.{hpp,cpp}`)

Ce n'est pas un multigrille algébrique du solveur linéaire mais une
**continuation de maillage de l'optimisation** : on optimise sur une grille
grossière, on prolonge le champ de densité, on continue sur la grille fine.

- Facteur 2 par niveau ; défauts `mg 96 3 25` = grille fine 96³, 3 niveaux,
  25 itérations/niveau (`src/main.cpp:129-135`, `MultiGridOptimizer.hpp:36-38`).
- **Prolongation** = injection : chaque cellule fine hérite la densité de son
  parent (`GridTransfer.cpp:5-17`) — volume conservé exactement.
- **Restriction** = moyenne des 8 enfants (`GridTransfer.cpp:19-35`) ;
  `restrict(prolongate(x)) = x`.
- Seules les **densités** sont héritées (warm-start du design) ; les
  déplacements repartent de zéro à chaque niveau.
- Mode `restart` vs `inherit` comparables en CLI (`src/main.cpp:135`).

### 2.6 SIMP + OC (`src/topopt/SIMP3D.{hpp,cpp}`)

`E(ρ) = Emin + ρᵖ(E0−Emin)` (`SIMP3D.cpp:12`), sensibilité
`dc/dρ = −p(E0−Emin)ρ^{p−1}·ce` avec `ce` calculé par `mf_strain_energy`.
Update Optimality Criteria : bisection sur le multiplicateur de volume, move
limit, clamp [0,1] (`SIMP3D.cpp:32-50`). Défauts du problème MBB :
60×20×20, volfrac 0.3, penal 3.0, rayon 1.5, move 0.2 (`src/main.cpp:28-38`).

### 2.7 La règle de précision du projet

**GPU = float32, oracles CPU = double.** Tous les buffers device sont
`float` ; les références CPU (`src/fem/FEM3D.cpp` : assemblage triplets +
`SimplicialLDLT` double, l.54) valident chaque brique GPU (`tests/test_cg_gpu.cpp`,
`test_fem3d.cpp`). Conséquence concrète du float32 itératif : `Emin = 1e-4`
et non 1e-9 — sinon le contraste de raideur rend K trop mal conditionnée dans
le vide et le PCG Jacobi cale (commentaire `src/main.cpp:32-34`, leçon
LL-006). Un solveur direct double tolère 1e-9 ; un itératif float non.

---

## 3. Le CPU multiphysique (double précision)

### 3.1 Pourquoi CPU double

Règle du projet : **correction d'abord** (ADR-018, `docs/DECISIONS.md:19-24`,
et `README.md:131-133` : « currently CPU double precision by design:
correctness first »). Les systèmes Stokes (point-selle indéfini) et CHT
(non-symétrique) sont résolus en **direct Eigen double** pour servir
d'oracles irréprochables aux gates de validation par différences finies. Le
portage GPU (MINRES/Uzawa matrix-free float32) est différé et devra être
re-validé contre ces oracles.

Solveurs par système (vérifiés dans le code) :

| Système | Propriété | Solveur | Fichier |
|---|---|---|---|
| Élasticité 3D (oracle) | SPD | `SimplicialLDLT` | `src/fem/FEM3D.cpp:54` |
| Élasticité axi 2D | SPD | `SimplicialLDLT` | `src/fem/FEM2DAxi.cpp:66` |
| Thermo-élastique v2 | SPD (2 blocs) | `SimplicialLDLT` | `src/adjoint/ThermoElasticAdjoint.cpp:109` |
| Stokes-Brinkman | indéfini (point-selle) | `SparseLU` | `src/physics/StokesSolver.cpp:197` |
| CHT (advection) | non-symétrique | `SparseLU` | `src/physics/CHTSolver.cpp:179` |
| Cascade triple + adjoints | mixte | `SparseLU` | `src/adjoint/TripleAdjoint.cpp:83` |

Dans tous les cas les Dirichlet sont imposées par **condensation** : une map
DOF global → index réduit (−1 si fixé), assemblage restreint aux lignes/
colonnes libres, ré-expansion à la fin (ex. `StokesSolver.cpp:109-118,
164-165, 202-206` ; lift pour Dirichlet non nuls dans `CHTSolver.cpp:152-169`).

### 3.2 Stokes Q1-Q1 PSPG (`src/physics/StokesSolver.{hpp,cpp}`)

- **4 DOF/nœud** `(u,v,w,p)`, indexation `dof(n,c) = 4n+c`
  (`StokesSolver.hpp:81`). Élément Q1-Q1 sur les mêmes cellules H8 unitaires
  → matrice élémentaire point-selle **32×32** (`StokesSolver.cpp:90-106`) :
  blocs visqueux `μ·Le` par composante, couplage pression-vitesse ±Gd,
  stabilisation PSPG `−τ·Le` sur le bloc pression.
- Q1-Q1 viole inf-sup → **PSPG obligatoire** (ADR-017, choisi contre
  Taylor-Hood pour rester sur la grille structurée matrix-free) avec
  `τ = α_stab·h²/μ`, `α_stab = 1/12` dans les tests (`StokesSolver.cpp:91`,
  `tests/test_stokes.cpp:31`). `test_stokes.cpp:140-152` démontre le damier de
  pression sans PSPG.
- **Brinkman** (ADR-019) : interpolation convexe de Borrvall-Petersson
  `α(γ) = α_max + (α_min−α_max)·γ(1+q)/(γ+q)`, défauts `α_min=0, q=0.1`,
  `α_max=1e4` calibré « fuite < 1 % à travers une dalle solide »
  (`StokesSolver.hpp:62,76-79`, `docs/DECISIONS.md:26-33`). Le terme
  `α(γ_e)·∫NaNb` s'ajoute aux blocs vitesse (`StokesSolver.cpp:171-173`).
- Assemblage sparse : triplets Eigen (32×32 = 1024 coefficients/élément),
  `setFromTriplets` + `makeCompressed` (`StokesSolver.cpp:141-195`).

### 3.3 CHT SUPG (`src/physics/CHTSolver.{hpp,cpp}`)

Advection-diffusion `−∇·(k(γ)∇T) + u·∇T = Q`, 1 DOF/nœud, conductivité
interpolée linéairement `k(γ) = ks + (kf−ks)·γ` (`CHTSolver.hpp:63-66`).
Stabilisation **SUPG** (ADR-020) :

```cpp
Pe = |u|·h/(2k);  ξ = coth(Pe) − 1/Pe  (ou Pe/3 en limite bas-Péclet)
τ  = h/(2|u|)·ξ                          // CHTSolver.cpp:50-55
```

Termes assemblés : diffusion `∫k∇w·∇T`, advection `∫w(u·∇T)`
(non-symétrique), SUPG `∫τ(u·∇w)(u·∇T)` + correction du terme source
(`CHTSolver.hpp:22-32`, assemblage Gauss 2×2×2 `CHTSolver.cpp:86-135`).
`tests/test_cht.cpp` montre l'oscillation Galerkin à Pe≈50 et la
monotonie SUPG.

### 3.4 TripleAdjoint (`src/adjoint/TripleAdjoint.{hpp,cpp}`)

Le cœur du v3. Couplage **one-way** γ → fluide → thermique → structure.
`TripleAdjoint::solve(gamma)` fait exactement, dans l'ordre :

**Forward (cascade)** :
1. Stokes-Brinkman : `A(γ)·w = f_s`, `w=(u,p)` de taille 4N
   (`TripleAdjoint.hpp:24`). NB : le CHT de la cascade est en **Galerkin pur,
   sans SUPG** (`TripleAdjoint.hpp:26`) — l'adjoint discret exige la
   transposée exacte de l'opérateur réellement assemblé.
2. CHT : `K_t(γ,u)·T = f_t`.
3. Thermo-élastique : `K_e(γ)·U = F_mech + F_th(γ,T)` avec
   `E(γ) = Emin + (E0−Emin)(1−γ)ᵖ` (convention : γ=1 = fluide) et charge
   thermique `F_th = Σ_e E_e·α_th·Cth·(T̄_e − Tref)` (`TripleAdjoint.hpp:29-31`).

**Adjoint (cascade inverse)** (`TripleAdjoint.cpp:478-579`) :
1. Élastique : `K_eᵀ·λ_e = −F_mech` (compliance ; auto-adjoint).
2. Thermique : `K_tᵀ·λ_t = Gᵀ·λ_e` où `G = ∂F_th/∂T` (RHS assemblé par
   `thermalAdjointRhs()`, l.417-436) — la transposition compte car
   l'advection est asymétrique.
3. Stokes : `Aᵀ·λ_s = −(∂R_t/∂u)ᵀ·λ_t` — le RHS de couplage
   `−∫λ_t·Na·(∂T/∂x_c)` est intégré élément par élément
   (`stokesAdjointRhs()`, l.440-476) ; composante pression nulle.

**Gradient** = 3 termes adjoints + décomposition retournée telle quelle :

```
grad_i = λ_eᵀ[(∂K_e/∂γ_i)U − ∂F_th/∂γ_i] + λ_tᵀ(∂K_t/∂γ_i)T + λ_sᵀ(∂A/∂γ_i)w
```

La struct `Solution {w, T, U, J, grad, termStokes, termThermal, termElastic}`
(`TripleAdjoint.hpp:64-73`) expose chaque terme séparément — les tests
vérifient que les trois sont non triviaux (sinon un couplage est mort).

**`solveStress` — le semis alternatif** (`TripleAdjoint.hpp:82-94,116-119`,
`.cpp:587-750`) : même cascade adjointe, mais la graine du premier système
n'est plus `−F_mech` : c'est `−∂J_σ/∂U` où `J_σ = (Σ σ_eᴾ)^{1/P}` est le
p-norm de von Mises relaxé `σ_e = s_e^q · vm0_e` (solidité `s_e = 1−γ_e`,
défauts `q=0.5, P=8`). S'ajoute un **terme explicite**
`∂J_σ/∂γ|exp ∝ q·s^{q−1}·vm0` (garde `s ≥ 1e-9`, l.674) puisque J dépend
directement de γ, pas seulement via U. Matrices constantes précalculées une
fois (cellules unitaires) : `ke0_` 24×24, `cth_` 24×8, `l0_` 8×8, `mvel_`
8×8, `stokesKe_` 32×32 (`TripleAdjoint.cpp:100-141`).

### 3.5 Les autres adjoints

- **ThermoElasticAdjoint** (`src/adjoint/ThermoElasticAdjoint.{hpp,cpp}`) —
  v2, cascade à 2 blocs (thermique SIMP `k(ρ)` → élasticité SIMP `E(ρ)` +
  charge thermique), gradient 3 termes {élastique, charge thermique,
  conduction}, variante von Mises p-norm avec relaxation `σ_e = ρ_e^q·vm0_e`.
- **DissipationAdjoint** (`src/adjoint/DissipationAdjoint.{hpp,cpp}`) —
  puissance dissipée de Borrvall-Petersson `Φ = ½wᵀHw` ; quasi auto-adjoint
  (`Aᵀλ = −Hw`, avec `λ_u ≈ −u` vérifié via `selfAdjResidual` dans la
  `Solution`) + terme explicite `½·dα_i·(uᵀM_vel u)_i`.
- **ThermalObjectiveAdjoint** (`src/adjoint/ThermalObjectiveAdjoint.{hpp,cpp}`)
  — température de paroi pic en p-norm `J_T = (Σ s_e·T̄_eᴾ)^{1/P}` (P=8),
  cascade CHT→Stokes + terme explicite en solidité.
- **AxiStressAdjoint** (`src/adjoint/AxiStressAdjoint.hpp`) + `src/fem/
  {AxiQ4Element,FEM2DAxi}` — chemin 2D axisymétrique Q4 (r,z), déformation
  circonférentielle `ε_θ = u_r/r`, intégration `Σ w·BᵀDB·(r_g·detJ)`.
  Point crucial (`AxiStressAdjoint.hpp:20-22`) : contrairement au 3D
  cartésien, la matrice E=1 **diffère par élément** (dépendance en r) — pas
  de `KE0` partagée.

### 3.6 MMAOptimizer (`src/topopt/MMAOptimizer.{hpp,cpp}`)

MMA de Svanberg (1987), résolu par **dualité** :

- Signature : `step(x, f0, df0dx, fvals, dfdx, xmin, xmax)` avec `fvals` (m
  contraintes ≤ 0) et `dfdx` (m×n) (`MMAOptimizer.hpp:43-45`).
- **Asymptotes** : init `x ∓ asyinit·range` ; puis heuristique
  d'oscillation — si `(x−xold1)(xold1−xold2) < 0` → resserre (γ=asydecr),
  sinon élargit (γ=asyincr) ; bornées à la Svanberg
  (`MMAOptimizer.cpp:32-56`). Défauts : `asyinit=0.5, asyincr=1.2,
  asydecr=0.7, albefa=0.1, move=0.5` (`MMAOptimizer.hpp:26-34`).
- **Sous-problème séparable** : primal en forme fermée par variable,
  `x_j(λ) = (√P_j·L_j + √Q_j·U_j)/(√P_j + √Q_j)` clampé aux move limits
  [α_j, β_j] (`MMAOptimizer.cpp:98-114`).
- **Dual** (`MMAOptimizer.cpp:193-268`) :
  - **m = 1** : bisection scalaire sur λ ∈ [0,∞), bracket puis 200 itérations
    max, tolérance relative 1e-12 ;
  - **m ≥ 2** : **Newton projeté** sur λ ∈ ℝ₊ᵐ — gradient + Hessienne du dual,
    pas `−H⁻¹g` régularisé, backtracking sur W(λ), repli gradient projeté,
    200 itérations max, stationnarité 1e-9.
- Historique `xold1_, xold2_` conservé pour l'update d'asymptotes
  (`MMAOptimizer.hpp:70-72`). Gate : `tests/test_mma.cpp` (optimum analytique
  + comparaison OC).

---

## 4. Infrastructure de validation

### 4.1 Les binaires de test

23 sources dans `tests/`, 23 binaires (`Makefile:72-96`). `make test_cpu`
exécute les **17 binaires CPU-purs** (aucun besoin de GPU ni de metallib,
`Makefile:246-264`) ; `make test` en exécute 22 en ajoutant les gates GPU
(`test_thermal`, `test_thermoelastic`, `test_cg_gpu`, `test_metal_hello`,
`test_mbb3d`) et `test_problemspec` (`Makefile:222-244`).

### 4.2 Méthodologie des gates DF (adjoints)

Chaque adjoint est validé contre une différence finie sur J, sur des sondes
aléatoires reproductibles : densités tirées dans **[0.3, 0.7]** (graine
12345u), éléments sondés tirés avec la graine 98765u — 18 ou 20 sondes selon
le test. Deux stencils :

- **Centré ordre 2** : `(J(+ε)−J(−ε))/2ε`, ε=1e-6
  (`test_adjoint_fd.cpp:83`, `test_triple_adjoint_fd.cpp:123`,
  `test_dissipation_adjoint_fd.cpp:106`).
- **Centré ordre 4** : `(−J(+2ε)+8J(+ε)−8J(−ε)+J(−2ε))/12ε`
  (`test_stress_adjoint_fd.cpp:106` avec ε=1e-4,
  `test_vm_triple_fd.cpp:138-141` et `test_tmax_adjoint_fd.cpp:111-113` avec
  ε=1e-6, `test_axi_stress_adjoint_fd.cpp:34` avec ε=1e-4).

Gates et résultats (résolutions : 8³ pour les 2-blocs, 6³ pour la cascade
triple, 16×16×1 pour la dissipation, 10×10 pour l'axi ; tolérances dans les
sources, valeurs atteintes tabulées dans `README.md:45-57`) :

| Gate | Critère (max err. rel.) | Atteint |
|---|---|---|
| thermo-élastique compliance | < 1e-5 | 1.6e-6 |
| von Mises 3D | < 1e-5 | 1.6e-7 |
| von Mises axi | < 1e-5 | 2.7e-9 |
| **triple couplé** (`test_triple_adjoint_fd.cpp:134`) | < 1e-3 | 2.1e-7 |
| dissipation | < 1e-4 | 7.2e-7 |
| T_max paroi | < 1e-3 | 7.5e-8 |
| von Mises via cascade triple | < 1e-3 | 8.0e-7 |

Les tests triples exigent en plus que les **trois termes de couplage soient
non triviaux** (‖term‖ > 1e-12 chacun) : un adjoint peut passer la DF avec un
bloc mort si l'objectif n'excite pas ce bloc.

### 4.3 LL-009 : arrondi vs vrai bug

Leçon documentée dans `archive/orchestration/LESSONS_LEARNED.md:152-167`. La
DF a un **plancher d'arrondi ≈ eps_machine·|J|/ε** : sur un élément à gradient
quasi nul, l'erreur *relative* explose alors que l'adjoint est exact. Le
diagnostic décisif est le **sweep en ε** : si l'erreur **décroît quand ε
grandit** → arrondi pur (adjoint bon) ; si elle croît → vraie erreur de
troncature ou adjoint faux. Sweep observé : ε=1e-6 → 1.6e-5, 1e-4 → 1.6e-7,
1e-3 → 3e-8 = signature d'arrondi. D'où : stencils d'ordre 4 avec ε élargi,
et jugement d'abord sur l'accord **absolu** (7-11 chiffres sur les éléments
bien conditionnés). Le sweep est codé en dur comme diagnostic dans
`test_axi_stress_adjoint_fd.cpp:111-122`.

### 4.4 Oracles physiques (non-régression systématique)

Chaque brique a un oracle analytique exécuté à chaque `make test_cpu` :

- **Élasticité 3D** : patch test + oracle direct double (`test_fem3d.cpp`).
- **Stokes** : Poiseuille plan `u_z = G·x(L−x)/2μ`, erreur nodale < 1 %,
  convergence O(h²), pression parasite < 1e-6 avec PSPG ; variante
  pressure-driven (traction ±p) pour valider les blocs B/Bᵀ
  (`test_stokes.cpp:27-179`).
- **Brinkman** : profil Darcy-Brinkman 1D en cosh + test de non-fuite
  (`test_brinkman.cpp`).
- **CHT** : solution 1D exacte `T = (e^{Pe·x/L}−1)/(e^{Pe}−1)`, O(h²) à Pe≈5,
  démonstration du piège Galerkin à Pe≈50 (`test_cht.cpp:42-127`).
- **Axi** : cylindre épais de Lamé, σ_θ et u_r < 2 % avec convergence nr=20→40
  (`test_axisymmetric.cpp:28-94`) ; le mapping trivial doit reproduire Lamé
  (`test_axi_mapped.cpp`).
- **Marching cubes** : sphère SDF R=0.35, aire vs 4πR² et volume vs 4/3πR³ à
  quelques %, maillage étanche (chaque arête partagée par exactement 2
  triangles), convergence 32→64 (`test_marching_cubes.cpp:38-91`).
- **GPU vs oracle** : `test_cg_gpu.cpp` (CG float vs LDLT double),
  `test_thermal.cpp`, `test_thermoelastic.cpp`, `test_multigrid.cpp`,
  `test_mbb3d.cpp` (chaîne complète).

---

## 5. Le contrat `.topopt.json`

### 5.1 ProblemSpec (`src/io/ProblemSpec.hpp`)

Struct unique parsée par nlohmann::json. **Parsing tolérant** : chaque champ
est lu par `j.value(key, default)` — un champ absent prend sa valeur par
défaut, jamais d'erreur ; c'est la base de la rétro-compatibilité (un vieux
spec reste valide, les nouveaux champs ont des défauts). Blocs et défauts
principaux (`ProblemSpec.hpp:14-191`) :

- `meta` : `name` ("problem"), `dim` ("3d" | "axi"), `version` (1).
- `domain` : `grid` [1,1,1], `size_mm` [1,1,1], `geometry` ("box" |
  "nozzle" avec `r_throat=0.6, K=0.20, wall=0.70`).
- `material` : `E0=1, Emin=1e-4, nu=0.3, penal=3` ; thermique `k_solid=1,
  k_fluid=0.3, alpha_th=1e-3, Tref=0` ; fluide `mu=1, brinkman_max=1e2,
  brinkman_q=0.1`.
- `physics` : tableau de strings, défaut `["elastic"]`.
- `bc` : listes `fixed / loads / pressure / thermal / flow` de `BCEntry`
  {sélecteur, `dof`, `value`} ; la valeur est lue en cascade
  `value → T → Q` (`ProblemSpec.hpp:118-147`) — rétro-compat des anciennes
  clés.
- `filter` : `radius_mm=1.5`, Heaviside `beta[]` (continuation) + `eta=0.5`.
- `optimize` : `objective="compliance"`, `optimizer="mma"`, `max_iter=60`,
  `penal_continuation=false`, `constraints[]` (type/bound).
- `output` : `dir="output"`, `formats=["vti"]`, `fields=["density"]`,
  `stl_iso=0.5`, `stl_method="marching_cubes"`.

Testé par `tests/test_problemspec.cpp` (défauts, parsing, résolution BC).
Exemples de référence dans `examples/*.topopt.json` (mbb3d v1,
bracket_thermo v2, cooling_jacket* v3, nozzle_profiled axi).

### 5.2 BCResolver (`src/io/BCResolver.hpp`)

Traduit les sélecteurs géométriques en masques/vecteurs de DOF sur la grille :

- **Sélecteurs** (`BCResolver.hpp:23-43`) : faces `"x-" "x+" "y-" "y+" "z-"
  "z+"` ; arête = intersection de 2 faces (`"x+,y-"`) ; coin =
  `"corner:x+,y-,z-"` ; région d'index `"z:8:12"` (plages de nœuds).
  `selectNodes()` retourne les indices de nœuds.
- **3 espaces de DOF** :
  1. *Élastique* — 3 DOF/nœud, `dof = 3n+c` ; `dof: "x"|"y"|"z"|"all"` ;
     `fixedMask()` (masque uint8 sur 3N) et `loadVector()` (charge répartie
     également : `value / nodes.size()` par nœud) (`BCResolver.hpp:47-67,
     184-189`).
  2. *Thermique* — 1 DOF/nœud ; `thermalFixedNodes()` (Dirichlet T) et
     `thermalSource()` (Q) (`BCResolver.hpp:76-99`).
  3. *Stokes* — 4 DOF/nœud, `dof = 4n+c` ; `stokesFixedDofs()` interprète
     `"wall"/"noslip"` (fixe u,v,w), `"slip"` (fixe la composante normale à
     la face), `"pressure"/"p"/"pdatum"` (fixe p) (`BCResolver.hpp:111-129`).

### 5.3 Le dispatch de `topopt_run` (`src/apps/topopt_run.cpp:1081-1121`)

Le driver choisit la branche sur `spec.dim` et l'ensemble `spec.physics` :

| Condition | Branche | Chemin de calcul |
|---|---|---|
| `dim=="axi"` et physics == {elastic} | `runAxiStruct()` (l.704-998) | CPU : FEM2DAxi + AxiStressAdjoint + MMA |
| `dim=="3d"` et physics == {elastic} | `runStructural()` (l.1000-1077) | **GPU** : CGSolver3D + Helmholtz3D + SIMP/OC (l.1011-1013) |
| `dim=="3d"` et {fluid, thermal, elastic} ⊆ physics | `runFluidThermal()` (l.327-601) | CPU : TripleAdjoint + MMA (+ contraintes tmax/dissipation/vonmises optionnelles) |
| `dim=="3d"` et {thermal, elastic}, sans fluid | `runThermoElastic()` (l.155-308) | CPU : ThermoElasticAdjoint + MMA (mass-min sous contrainte de contrainte) |

Codes de sortie : 0 succès, 1 erreur (parsing/GPU/contraintes), 2 usage,
3 dim/physique non supportée. La continuation Heaviside découpe `max_iter`
en paliers égaux sur les valeurs de `filter.heaviside.beta[]`
(`betaAt()`, l.146-153).

---

## 6. Les sorties

- **VTKExporter** (`src/io/VTKExporter.hpp:19-61`) : header-only, écrit du
  VTK XML ImageData `.vti` en **ASCII**, `DataArray type="Float32"`
  (l.46-47), champs cellulaires dans l'ordre VTK x-fastest. Champs selon la
  branche : `density`, `vonMises`, `temperature`, `displacement`, `speed`.
- **STLExporter** (`src/io/STLExporter.cpp:1-95`) : **STL binaire** (header
  80 octets + uint32 + triangles de 50 octets). Deux surfaces : voxel brute
  (2 triangles par face exposée, normales CCW sortantes) et, pour l'axi, la
  révolution du profil 2D (utilisée par les apps nozzle).
- **MarchingCubes** (`src/io/MarchingCubes.{hpp,cpp}`) : tables canoniques
  Lorensen-Cline 256 cas (≤ 5 triangles/cellule), interpolation linéaire sur
  les 12 arêtes `t = (iso−f0)/(f1−f0)`. Sélectionné par
  `output.stl_method="marching_cubes"` ; validé sur la sphère (§ 4.4).
- **PngWriter** (`src/io/PngWriter.hpp:8-133`) : writer PNG **maison, sans
  zlib** — 8-bit grayscale, blocs DEFLATE « stored » (non compressés).
  Utilisé par la branche axi pour l'aperçu du profil (vue pleine largeur
  2·r, axe marqué en gris).

Toutes les branches de `topopt_run` écrivent dans `output.dir` (le serveur y
injecte un dossier horodaté, cf. § 7).

---

## 7. Le serveur de run (`server/run-server.mjs`, 411 lignes)

**Node ≥ 20 natif, zéro dépendance npm** — uniquement `node:http`,
`node:child_process`, `node:fs`, `node:path` (l.21-25). Défauts :
`127.0.0.1:8787`, options `--host/--port` (l.34-37). CORS ouvert (`*`,
l.237-241).

**API** (spec en tête de fichier l.10-18, handlers l.337-386) :

| Méthode | Route | Rôle |
|---|---|---|
| GET | `/api/health` | `{ok, version, solver}` |
| POST | `/api/run` | corps = `.topopt.json` → `{jobId}` ; **409** si un run est déjà en cours ; 503 si `build/topopt_run` absent |
| GET | `/api/jobs/:id` | état du job (running/done/failed/canceled) |
| GET | `/api/jobs/:id/log` | **SSE** : replay de l'historique puis flux live |
| DELETE | `/api/jobs/:id` | kill du job (groupe de processus entier) |
| GET | `/api/jobs/:id/artifacts/<f>` | sert un fichier produit |

- **SSE** : chaque ligne de sortie devient `{type:"line", stream, line}`,
  stockée dans `job.lines` (replay pour les clients tardifs) et diffusée aux
  abonnés ; à la fin `{type:"exit", exitCode, status, artifacts[]}`
  (l.130-153, 302-326).
- **Le problème pty** : lancé avec stdout dans un pipe, le solveur (stdio C)
  passe en **full buffering** → aucun log live. Solution (l.106-123) :
  spawn via `/usr/bin/script -q /dev/null <solver> <spec>` qui alloue un
  pseudo-terminal, forçant le line buffering ; `detached: true` pour pouvoir
  tuer tout le groupe (`kill(-pid)`). Les artefacts pty (`\r`, `^D`,
  backspaces) sont nettoyés par `cleanLine()` (l.126-128).
- **Sécurité** : un **seul job à la fois** (`if (currentJob) → 409`,
  l.345-349, le solveur sature la machine) ; anti path-traversal sur les
  artefacts (`path.resolve` + vérification du préfixe `job.dir + path.sep`
  → 403, l.276-286) ; bind local par défaut.
- **Runs horodatés** : dossier `output/<name>-YYYYMMDD-HHMMSS` (l.72-79,
  167) ; le spec effectivement exécuté (avec `output.dir` réécrit) est
  **archivé dans le dossier du run** (`<name>.topopt.json`, l.172-174) —
  chaque résultat est reproductible.

---

## 8. Le Studio (`web/`)

### 8.1 Stack

Vite 7 + TypeScript 5.8 **strict intégral** (`web/tsconfig.json:10` :
`strict`, `noUncheckedIndexedAccess`, `exactOptionalPropertyTypes`, …),
Vitest 3.2. Dépendances runtime (`web/package.json:14-18`) : `three ^0.178.0`
(éditeur 3D), `@kitware/vtk.js ^36.4.1` (viewer résultats), `lil-gui ^0.20.0`.

Arborescence `web/src/` : `spec/` (contrat), `editor/` (scène three.js),
`panels/` (UI paramètres/BC/run/export), `viewer/` (résultats vtk.js),
`state.ts` (store), `main.ts` (bootstrap).

### 8.2 Miroir TS du contrat — le C++ est la source de vérité

- `web/src/spec/ProblemSpec.ts` : interface TS reflétant champ pour champ
  `src/io/ProblemSpec.hpp` (mêmes blocs, mêmes défauts via `defaultSpec()`).
- `web/src/spec/serialize.ts` : `specToJson()` **omet les scalaires égaux aux
  défauts** (le parseur C++ tolérant les restaure), et garantit l'invariant
  round-trip `specFromJson(specToJson(s)) ≡ s` (`serialize.ts:1-6`, testé
  par `web/tests/roundtrip.test.ts`).
- **Golden test** : `web/tests/golden.test.ts` construit l'état mbb3d par
  l'API interne de l'UI et exige l'égalité avec
  `examples/mbb3d.topopt.json` — le fichier consommé par le C++. Toute
  divergence de contrat casse la CI web. Complété par `compat.test.ts` et
  `presets.test.ts`.

### 8.3 Éditeur three.js

`web/src/editor/scene.ts` : boîte du domaine + plans de picking ;
**raycaster** (`THREE.Raycaster` + `intersectObjects(this.pickPlanes)`,
l.40-41, 215-224) pour la sélection de faces au survol/clic (surbrillance
par opacité), Shift+clic pour les arêtes (arête canonique calculée dans
`ProblemSpec.ts:144-147`). Les BC sont matérialisées par des glyphes
(`editor/glyphs.ts`).

### 8.4 Viewer résultats — vtk.js en chunk lazy

- vtk.js (~lourd) vit dans un **chunk séparé chargé dynamiquement** : premier
  passage sur l'onglet Results (ou premier drop de fichier) →
  `import("./viewer/viewer")` mémoïsé dans `ensureViewer()`
  (`web/src/main.ts:29-51`). Le premier chargement du Studio ne paie jamais
  vtk.js.
- vtk.js n'expédie pas de types TS pour tous ses modules → **typages locaux**
  `web/src/viewer/vtk-modules.d.ts` ; imports concentrés dans
  `viewer/vtk.ts` (profils OpenGL Geometry/Volume, GenericRenderWindow,
  ImageMarchingCubes, etc.).
- **Parseurs maison, sans framework** : `viewer/vtiParser.ts` (XML ImageData
  → typed arrays) et `viewer/stlParser.ts` (STL binaire via DataView) —
  testés par `web/tests/vti.test.ts` et `stl.test.ts`. vtk.js ne sert qu'au
  rendu ; le décodage des fichiers est contrôlé par le projet.

### 8.5 Connexion au serveur de run

`web/src/panels/runPanel.ts` : l'hôte/port du serveur distant sont persistés
en **localStorage** sous les clés `topopt.remoteHost` / `topopt.remotePort`
(l.116-135) ; `web/.env.local` (gitignoré) peut les pré-remplir via
`VITE_REMOTE_HOST` / `VITE_REMOTE_PORT` (l.113-115). Le panel poste le spec
sérialisé sur `/api/run`, suit le log en SSE et charge les artefacts dans le
viewer.

---

## 9. Performances et limites actuelles

Chiffres relevés dans le code et les rapports du dépôt
(`archive/orchestration/LESSONS_LEARNED.md`, `archive/**/PHASE_5_REPORT.md`,
`README.md`) :

| Dispatch | Grille type | Étage | Repère mesuré |
|---|---|---|---|
| v1 structurel 3D | 60×20×20 (défaut, 24k élém.) | GPU (CG + filtre) | 60 it ≈ 52 s après l'optimisation « un filtrage/itération » (LL-007) ; bench FEM seul jusqu'à 128³ (`src/main.cpp:126`), multigrid 96³/3 niveaux (`src/main.cpp:130-131`) |
| axi | 2D (nr×nz), ex. nozzle profilé | CPU | interactif (LDLT 2D) |
| v2 thermo-élastique | ~8³-32³ | CPU double | solveurs directs, mémoire O(nnz) de la factorisation |
| v3 fluide-thermique | 12×12×20 (2 880 élém.) | CPU double | 60 itérations ≈ 327 s (démo cooling_jacket, PHASE_5_REPORT) |

**Ce qui est GPU aujourd'hui** : uniquement la branche v1 (élasticité
matrix-free float32 + filtre Helmholtz + énergie de déformation) et le
solveur thermique de `src/physics/ThermalSolver.cpp`.
**Ce qui est CPU** : tout le multiphysique (Stokes, CHT, cascade triple,
tous les adjoints, MMA) et l'axi — par choix (ADR-018 : oracles double
précision d'abord).

**Limites structurelles actuelles** :
- Le v3 est borné par les factorisations `SparseLU` du point-selle 4N×4N
  (double, CPU) — praticable pour les gates et démos (6³-12×12×20), pas pour
  des grilles de production.
- Le float32 GPU impose `Emin=1e-4` (conditionnement, LL-006) là où un direct
  double accepte 1e-9.
- Le serveur exécute un run à la fois (par conception, l.345-349).

**Plan de portage** (README « GPU port of the multiphysics adjoints »,
ADR-018) : solveur point-selle itératif matrix-free (MINRES/Uzawa) en float32
sur Metal, systématiquement re-validé contre les oracles CPU double et les
gates DF existants — la même discipline que pour le chemin v1.
