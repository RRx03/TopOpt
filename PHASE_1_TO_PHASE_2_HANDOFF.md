# PHASE_1_TO_PHASE_2_HANDOFF.md

## Vue d'ensemble du projet TopOpt

**Objectif final** : Solveur de topology optimization multiphysique 
(fluide-structure-thermique) en C++/Metal sur Apple Silicon, vocation 
démonstrateur industriel pour conception de tuyères et chambres de 
combustion régénératives.

**Architecture en 5 phases** :
1. Phase 1 (en cours / terminée) : TO structurelle 2D SIMP, MBB beam canonique
2. Phase 2 (à démarrer) : Passage 3D + Metal GPU compute pour FEM
3. Phase 3 : Multi-grid uniforme + mesh independence
4. Phase 4 : Couplage thermo-élastique + contraintes von Mises + 2D axi
5. Phase 5 : Couplage Stokes + CHT, TO multiphysique complète

**Hardware cible** : Mac Studio Apple Silicon (M-series), 64 GB unified memory

---

## État du code en sortie de Phase 1

### Arborescence actuelle
```
TopOpt/
├── Makefile
├── README.md, CLAUDE.md, TRANSITIONS.md, .gitignore
├── src/
│   ├── main.cpp                  // Entry point, dispatch sur cas test
│   ├── core/
│   │   ├── Grid2D.{hpp,cpp}      // Structure grille uniforme 2D
│   │   └── Mesh2D.{hpp,cpp}      // Connectivité éléments-nœuds
│   ├── fem/
│   │   ├── ElementMatrix.{hpp,cpp}  // Matrice rigidité élémentaire Q1
│   │   ├── Assembly.{hpp,cpp}    // Assembly K globale en CSR
│   │   └── LinearSolver.{hpp,cpp}   // Wrapper Eigen SimplicialLLT
│   ├── topopt/
│   │   ├── SIMP.{hpp,cpp}        // Loi de pénalisation densité
│   │   ├── ObjectiveCompliance.{hpp,cpp}  // J(ρ,U) et ∂J/∂ρ
│   │   └── OptimalityCriteria.{hpp,cpp}   // Update OC avec move-limit
│   ├── filter/
│   │   └── HelmholtzFilter.{hpp,cpp}  // Filtre densité Poisson
│   ├── io/
│   │   ├── PNGWriter.{hpp,cpp}   // Visualisation via stb_image_write
│   │   └── JSONLoader.{hpp,cpp}  // Lecture problem.json
│   └── benchmarks/
│       ├── MBBBeam.{hpp,cpp}     // Cas canonique MBB
│       └── Cantilever.{hpp,cpp}  // Cas validation croisée
├── third_party/
│   ├── eigen/                    // Eigen 3.4 header-only
│   ├── nlohmann/                 // json.hpp single-header
│   └── stb/                      // stb_image_write.h
├── tests/
│   ├── test_patch.cpp            // Test patch FEM (champ uniforme)
│   ├── test_cantilever.cpp       // Validation flèche analytique
│   └── test_mbb.cpp              // Validation compliance ≈ 200
├── build/
└── assets/
    ├── problem_mbb.json
    └── problem_cantilever.json
```

### Conventions de code établies (à respecter en Phase 2)

- C++23, indentation 2 espaces, K&R braces
- PascalCase classes, camelCase fonctions, UPPER_SNAKE constantes
- `#pragma once`
- RAII strict, aucun new/delete brut, `std::unique_ptr` ou stack
- `[[nodiscard]]` sur retours non-triviaux
- `const` correctness systématique
- `std::span`, `std::string_view` préférés
- Variables/fonctions en anglais, commentaires explicatifs français OK
- Floating point : `double` partout (Phase 1). Phase 2 considèrera `float`
  sur GPU.

### Conventions FEM établies

- Éléments Q1 quadrilatères bilinéaires (2D) → migration vers H8 hexaèdres
  trilinéaires (Phase 2)
- Numérotation locale des nœuds : anti-horaire stricte (détermine signe du
  Jacobien)
- Stockage matrice K : `Eigen::SparseMatrix<double>` format CSR
- Solveur linéaire Phase 1 : `Eigen::SimplicialLLT` (direct, Cholesky creux)
  → migration CG préconditionné Jacobi sur GPU (Phase 2)
- Conditions limites : zero-out row AND column (préserve symétrie de K)

### Conventions TO établies

- ρ ∈ [ρ_min, 1] avec ρ_min = 1e-3 (jamais zéro strict, sinon K singulière)
- Loi SIMP : E(ρ) = E_min + ρ^p × (E_solid - E_min)
- p de SIMP : continuation 1 → 2 → 3 sur 30 itérations chacune
- Filtre Helmholtz : -r² ∇²ρ̃ + ρ̃ = ρ, r en cellules (Phase 1) puis en mm 
  (Phase 3)
- OC update : move-limit = 0.2, exposant η = 0.5
- Critère d'arrêt : max|Δρ| < 1e-2 sur deux itérations consécutives, ou
  200 itérations max

### Cas tests validés en Phase 1

1. **Test patch** : domaine 4×4, champ de déplacement uniforme imposé aux 
   bords. Le solveur reproduit le champ exactement (erreur < 1e-12 en 
   double). → VALIDÉ
2. **Test cantilever analytique** : poutre console encastrée, charge à 
   l'extrémité, comparaison flèche analytique Euler-Bernoulli. Erreur 
   < 1% pour maillage 100×20. → VALIDÉ
3. **Test MBB canonique** : domaine 200×100, vol=0.5, p=3 (avec continuation), 
   r=2 cellules. Compliance finale ≈ 200, design en voûte/treillis visible. 
   → VALIDÉ
4. **Test cantilever TO** : domaine 200×100, vol=0.4, design en harpe/éventail 
   depuis l'encastrement. Validation croisée des BCs. → VALIDÉ

### Cas test optionnels exécutés (si bonus)

- MBB à plusieurs résolutions (50×25, 100×50, 200×100, 400×200) → 
  convergence visuelle vers même topologie
- Sensibilité au rayon de filtre r : r=1 produit checkerboards (filtre 
  insuffisant), r=2 design propre, r=4 design simplifié

### Dette technique acceptée en sortie de Phase 1

- Visualisation : PNG only, pas de viewer interactif (Phase 2 ajoutera)
- Solveur linéaire : direct (LLT), ne scale pas en 3D (Phase 2 migre vers CG)
- Pas de parallélisation CPU (Phase 1 reste single-thread, suffisant 2D)
- Filtre Helmholtz : résolution par Eigen SimplicialLLT directe (acceptable
  taille 2D, à migrer GPU en Phase 2)
- Pas de tests automatisés CI (validation manuelle)
- Magic numbers : exposés comme constantes nommées dans `Constants.hpp`, OK

---

## Préparation de Phase 2

### Objectif Phase 2

Migrer le solveur en 3D et exploiter la GPU Metal d'Apple Silicon. Le défi 
n'est pas algorithmique (la TO est identique) mais maîtrise de Metal pour 
le FEM creux à grande échelle.

### Modifications principales requises

| Composant | Phase 1 (acquis) | Phase 2 (à coder) |
|---|---|---|
| Dimension | 2D quadrilatères Q1 | 3D hexaèdres H8 (trilinéaires) |
| Indexation grille | (i,j) row-major | (i,j,k) row-major + clé Morton optionnelle |
| Élément matrice | Q1 → 8×8 (2D, 2 DOF/nœud) | H8 → 24×24 (3D, 3 DOF/nœud) |
| Assembly | CPU loop, Eigen | Compute kernel Metal, format CSR custom |
| Solveur linéaire | Direct (LLT) | CG préconditionné Jacobi sur GPU |
| Format K | Eigen::SparseMatrix | MTLBuffer CSR (values, col_idx, row_ptr) |
| Stockage U, F, ρ | std::vector<double> | MTLBuffer float (typiquement) |
| Précision | double partout | float sur GPU, double sur CPU pour init |
| Filtre Helmholtz | Direct Eigen | CG sur GPU, même format CSR |
| Visualisation | PNG 2D | Marching cubes → STL → viewer externe |
| Taille typique | 200×100 = 20k DOF | 128³ = 6.3M DOF (×3 = 19M DOF avec 3 directions) |
| Temps cible/itération | < 0.3 s | < 5 s |
| Temps cible/optim complète | < 30 s | < 10 minutes |

### Architecture cible Phase 2

```
TopOpt/
├── Makefile (modifié : link Metal, Foundation, QuartzCore)
├── src/
│   ├── (tout l'existant Phase 1, ajusté pour 3D)
│   ├── core/
│   │   ├── Grid3D.{hpp,cpp}      // NEW
│   │   └── Mesh3D.{hpp,cpp}      // NEW
│   ├── fem/
│   │   ├── ElementMatrixH8.{hpp,cpp}  // NEW : hexaèdre trilinéaire
│   │   └── AssemblyGPU.{hpp,cpp}      // NEW : assembly via Metal
│   ├── metal/                         // NEW SECTION
│   │   ├── MetalContext.{hpp,cpp}     // device, queue, pipelines
│   │   ├── MetalBuffer.{hpp,cpp}      // wrapper MTLBuffer
│   │   ├── CGSolverMetal.{hpp,cpp}    // CG sur GPU
│   │   └── SpMVKernel.{hpp,cpp}       // multiplication matrice-vecteur creuse
│   ├── io/
│   │   ├── STLExporter.{hpp,cpp}      // NEW
│   │   └── MarchingCubes.{hpp,cpp}    // NEW : extraction iso ρ=0.5
│   └── benchmarks/
│       ├── MBBBeam3D.{hpp,cpp}        // NEW : MBB extrudé en 3D
│       └── Cantilever3D.{hpp,cpp}     // NEW : validation
├── shaders/                           // NEW
│   ├── assembly.metal
│   ├── spmv.metal
│   ├── cg_kernels.metal               // saxpy, dot, axpy, etc.
│   └── helmholtz.metal
├── third_party/
│   ├── metal-cpp/                     // NEW
│   └── (existant)
└── tests/
    ├── test_h8_element.cpp            // NEW : validation matrice élémentaire H8
    ├── test_assembly_gpu.cpp          // NEW : comparaison CPU vs GPU
    ├── test_cg_metal.cpp              // NEW : CG converge sur cas analytique
    └── test_mbb_3d.cpp                // NEW : MBB 3D canonique
```

### Dépendance nouvelle : metal-cpp

- Source : https://developer.apple.com/metal/cpp/
- Single header : `Metal/Metal.hpp`, `Foundation/Foundation.hpp`, 
  `QuartzCore/QuartzCore.hpp`
- Vendored dans `third_party/metal-cpp/`
- Lien : `-framework Metal -framework Foundation -framework QuartzCore`

### Priorités d'implémentation Phase 2 (ordre)

1. **Setup Metal** : MetalContext, vérification que device existe, queue 
   créée, kernel compute trivial (add deux vecteurs) tourne. Test : 
   somme deux MTLBuffer<float> de 1M éléments matche CPU.
2. **Élément H8** : matrice rigidité 24×24 d'un hexaèdre cube unit. Test : 
   reproduire valeurs de référence (Belytschko, Liu, Moran 2014 ch. 9).
3. **Grid3D + Mesh3D + Assembly CPU** : assembly K globale en CSR sur CPU 
   d'abord (validation). Test : cantilever 3D analytique.
4. **Assembly GPU** : kernel Metal qui assemble K en CSR. Test : K_GPU == 
   K_CPU à 1e-6 près en float.
5. **SpMV GPU** : multiplication matrice-vecteur creuse. Test : K·U_unit 
   matche CPU.
6. **CG sur GPU** : opérations vectorielles (dot, axpy, saxpy) + boucle CG. 
   Test : convergence sur cas analytique cantilever 3D.
7. **Préconditionneur Jacobi** : diagonale de K, multiplication par 
   l'inverse. Speedup attendu : 2-5×.
8. **Filtre Helmholtz GPU** : assembly de l'opérateur Helmholtz + CG sur GPU.
9. **Loop TO complète** : intégration de tout, MBB 3D, écriture STL.
10. **Visualisation** : marching cubes sur ρ pour iso=0.5, export STL, 
    ouverture dans MeshLab ou Preview.

### Pièges spécifiques Phase 2 (cf. TRANSITIONS.md détails)

- **Race conditions assembly GPU** : nœuds partagés entre éléments → 
  atomicAdd obligatoire ou stratégie de coloring d'éléments
- **Précision float32 sur GPU vs double sur CPU** : seuils de convergence 
  CG à ajuster (1e-6 typique en float)
- **Coalescence mémoire** : ordre d'accès aux DOF doit suivre l'ordre 
  Morton ou row-major selon kernel design
- **Tailles de threadgroups Metal** : tester 256, 512, 1024, garder 
  l'occupancy max
- **Memory pressure** : surveiller pic mémoire à 128³, ne pas dépasser 
  ~30 GB (laisser marge système)

### Validations obligatoires fin Phase 2

- Test patch FEM 3D passe à 1e-6 (float) ou 1e-10 (double)
- Cantilever 3D analytique : flèche reproduite à <2%
- MBB 3D : design symétrique correct, compliance dans la fourchette 
  attendue par les références (Aage 2015 typiquement)
- Convergence CG sur cas test : résidu / résidu_initial < 1e-6 en moins 
  de 2000 itérations
- Benchmarks : 128³ converge en < 10 minutes wall-clock sur M-series
- STL extrait s'ouvre dans MeshLab sans erreur, manifold, surface fermée

### Documentation à produire fin Phase 2

- `PHASE_2_REPORT.md` : choix d'implémentation, benchmarks, écarts
- `TRANSITIONS.md` mis à jour avec acquis Phase 2
- Comparaison CPU vs GPU : temps et précision, sur tableau de cas tests
- Screenshots/GIFs : convergence d'optim, design 3D du MBB

---

## Métadonnées du projet (pour Fable 5 plus tard)

### Décisions architecturales prises et justifiées

1. **Single source of truth = SIMP** : on n'utilise PAS level-set ni 
   phase-field. SIMP est mature, simple, suffisant pour tous nos cas 
   Phase 1-5. Justification : ratio simplicité/performance optimal, 
   communauté académique fortement standardisée.
2. **Grille uniforme dense Phase 2-3** : pas d'octree ni AMR. Justification : 
   sur Apple Silicon 64 GB, 256³ tient en mémoire. AMR introduit 
   complexité massive (cf. TRANSITIONS.md Phase 6) sans gain proportionné 
   pour notre échelle.
3. **Metal-cpp plutôt qu'Objective-C/Swift** : reste en C++ pur, pas de 
   mélange languages. Justification : maintenabilité, portabilité 
   intellectuelle (toute la communauté CFD/FEM est en C++).
4. **CG sur GPU Phase 2, multigrid Phase 3** : on monte en sophistication 
   par étapes. Justification : valider le simple avant le complexe.
5. **Filtre Helmholtz plutôt que filtre de convolution** : équation EDP 
   linéaire, indépendant de la connectivité du voisinage, scalable 
   parallèle. Justification : Lazarov-Sigmund 2011 démontre la supériorité.

### Décisions à reporter (Phase 4-5 territory)

1. Choix entre Brinkman penalization, level-set, phase-field pour TO 
   fluide : à arbitrer rigoureusement avec étude comparative en Phase 5
2. Choix entre adjoint continu et adjoint discret : adjoint discret 
   probablement, justification : compatible automatic differentiation 
   et plus simple à valider par DF
3. Choix de l'algorithme MMA vs GCMMA en Phase 4 : démarrer MMA, migrer 
   GCMMA si oscillations

### Points de vigilance permanents

- Validation par différences finies du gradient adjoint : OBLIGATOIRE 
  dès qu'on touche l'adjoint multi-bloc (Phase 4+)
- Mesh independence : tester explicitement à chaque ajout de physique
- Stress singularity : connue, à gérer en Phase 4 (ε-relaxation Duysinx)
- Inf-sup condition pour Stokes : Taylor-Hood safe, à respecter en Phase 5
- Précision flottante : double sur CPU, float sur GPU = OK si CG converge 
  proprement, à monitorer

### Bibliographie de référence du projet

Lectures obligatoires faites ou à faire :
- Andreassen et al. 2011, *Struct. Multidisc. Optim.* 43:1 (88-line, base 
  Phase 1) [LU]
- Bendsøe & Sigmund 2003, *Topology Optimization*, Springer (ch. 1-3) 
  [À LIRE]
- Allaire, *Conception optimale de structures*, Springer 2007 [À LIRE - 
  recommandé en priorité car français + complet]
- Lazarov-Sigmund 2011, *Int. J. Numer. Methods Eng.* 86:765 (Helmholtz 
  filter) [LU]
- Aage-Lazarov 2013, *Struct. Multidisc. Optim.* 47:493 (parallel) 
  [Phase 2]
- Aage et al. 2017, *Nature* 550 (large-scale, multigrid) [Phase 3]
- Pedersen-Pedersen 2010, *Struct. Multidisc. Optim.* 42:681 (thermo-élastique) 
  [Phase 4]
- Le-Norato-Bruns 2010, *Struct. Multidisc. Optim.* 41:605 (stress-constrained) 
  [Phase 4]
- Borrvall-Petersson 2003, *Int. J. Numer. Methods Fluids* 41:77 (Brinkman) 
  [Phase 5]
- Alexandersen-Andreasen 2020, *Fluids* 5(1):29 (review TO fluide) 
  [Phase 5]
- Dilgen et al. 2018, *Struct. Multidisc. Optim.* 57:1905 (TO multiphysique 
  adjointe) [Phase 5]

### Identité du développeur (pour cohérence relationnelle)

- Étudiant ISAE-SUPAERO, niveau avancé, MSc en cours
- Mac Studio M-series 64 GB, environnement de développement principal
- Langages : C/C++ avancé, Metal API + Metal Shading Language, Python 
  scientifique
- Domaines de compétence : aéronautique, spatial, IA, numérique, GPU
- Approche pédagogique : préfère comprendre les concepts avant de coder, 
  apprécie les explications conceptuelles brèves avant l'implémentation
- Attentes : pas de validation polie, critique honnête, rigueur senior, 
  refus explicite des mauvaises approches avec justification
- Communication : français privilégié pour explications, anglais pour 
  code/identifiants
- Vision : projet à vocation industrielle/recruteur, pas exercice 
  académique. Validation rigoureuse et benchmarks contre littérature 
  publiée sont des livrables critiques.

### Niveau de connaissance actuel (juin 2026)

- Maîtrise : FEM élastique linéaire 2D/3D, SIMP, OC, gradient adjoint 
  analytique pour compliance (Phase 1)
- En cours d'acquisition : Metal compute, CG préconditionné sur GPU, 
  H8 élément (Phase 2)
- À acquérir Phase 3-5 : multi-grille, adjoint multi-bloc, couplage 
  thermo-fluide-élastique, Brinkman, p-norm aggregation
- Notions générales déjà acquises : tuyère de Laval, équations Euler, 
  Navier-Stokes (cours ISAE), méthodes numériques classiques
