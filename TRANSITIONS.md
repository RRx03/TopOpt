> **⚠️ Référence historique (depuis le reset documentaire du 2026-06-26).**
> Ce document reste la cartographie détaillée d'origine des phases, mais il est
> **superseded opérationnellement** par l'orchestration unifiée :
> - Vue d'ensemble : `../orchestration/ROADMAP.md`
> - Détail par phase : `../orchestration/prompts/PHASE_N_BRIEF.md`
> - Passations : `../orchestration/handoffs/`
> - Conventions (alignées sur le **code réel**) : `../orchestration/MASTER_CLAUDE.md`
>
> En cas de conflit, **le code fait foi** (`../analysis/CODE_ANALYSIS.md`), puis
> l'orchestration, puis ce document.

# TRANSITIONS.md — Cartographie des phases du projet TopOpt

**Objet** : Pour chaque phase, lister ce qui est acquis (et fiable), ce qui
reste fragile (à valider rigoureusement avant d'avancer), ce qui doit être
ajouté/modifié pour la phase suivante, et les pièges connus.

**Mise à jour** : ce document est vivant. À chaque fin de phase, tu valides
les checkpoints, tu notes les écarts par rapport au plan, tu mets à jour les
listes "fragilités" et "dette technique" avant de passer à la phase suivante.

**Règle d'or** : ne JAMAIS passer à la phase N+1 si les checkpoints de la
phase N ne sont pas tous verts. La dette technique en TO multiphysique est
exponentielle, pas linéaire.

---

## Phase 1 — TO structurelle 2D, SIMP, adjoint analytique

### Objectif

Implémenter le solveur de référence Andreassen et al. 2011 en C++.
Le cas MBB beam est le benchmark canonique de toute la communauté TO.

### Acquis en sortie de phase (doit être vrai à 100%)

- ✓ Assembly FEM 2D quadrilatères Q1 bilinéaires fonctionnel
- ✓ Solveur linéaire direct (SimplicialLLT Eigen) sur K U = F
- ✓ Champ de densité ρ ∈ [ρ_min, 1] sur grille uniforme régulière
- ✓ Loi SIMP : E(ρ) = E_min + ρ^p × (E_solid - E_min)
- ✓ Calcul du gradient adjoint analytique pour compliance
- ✓ Filtre Helmholtz de densité (équation de Poisson)
- ✓ Update Optimality Criteria avec move-limit
- ✓ Critère d'arrêt convergence : Δρ_max < tolérance
- ✓ Visualisation PNG du champ ρ à chaque itération
- ✓ Test patch : champ uniforme retrouvé exactement
- ✓ Test poutre console : flèche analytique reproduite à <1%
- ✓ Test MBB : design canonique reproduit (vérification visuelle + compliance)

### Fragilités potentielles à valider rigoureusement

**À VÉRIFIER avant de passer à Phase 2** :

1. **Checkerboarding** : si ton design final montre des damiers (alternance
   ρ=0/ρ=1 cellule par cellule), ton filtre Helmholtz est mal implémenté.
   Symptôme classique d'un projet TO bâclé.
2. **Mesh dependence non testée** : refais MBB à 100×50, 200×100, 400×200.
   Les designs doivent converger visuellement vers la même structure (à
   raffinement près). Si non, le filtre Helmholtz ne fait pas son job.
3. **Continuation de p** : si tu démarres directement à p=3, OC peut osciller.
   Continuation : p = 1 → 2 → 3 sur ~30 itérations chacune.
4. **Stabilité numérique de ρ_min** : ρ_min = 1e-3 minimum, jamais zéro
   strict (sinon K devient singulière dans les zones vides).
5. **Conservation du volume** : à chaque itération, vérifier Σρ × dV ≈
   V_target (±0.1%). Si dérive, bug dans OC update.

### Dette technique acceptable à ce stade

- Visualisation PNG only (pas de viewer interactif)
- Pas de tests CI automatisés (validation manuelle OK)
- Pas de gestion mémoire optimisée (Eigen dense pour petits cas)
- Pas de parallélisation (single thread OK pour 2D)

### Ce qui doit changer en Phase 2

| Composant | Phase 1 | Phase 2 |
|---|---|---|
| Dimension | 2D quadrilatères | 3D hexaèdres |
| Solveur linéaire | Direct (LLT) | Itératif CG préconditionné |
| Backend | CPU Eigen | GPU Metal compute shaders |
| Stockage matrices | Eigen::SparseMatrix | Format CSR custom + buffers Metal |
| Taille typique | 200×100 = 20k DOF | 128³ = 6M DOF |
| Assembly | Loop CPU | Compute kernel Metal |

### Pièges spécifiques Phase 1

- **Sign error sur les forces** : la force est négative en y si elle pointe
  vers le bas. Erreur de signe = design inversé silencieux.
- **Ordre des nœuds dans l'élément** : convention anti-horaire stricte,
  sinon le déterminant du Jacobien est négatif et K perd sa positivité.
- **Conditions limites mal imposées** : zero-out the row AND the column,
  pas juste la ligne. Sinon K asymétrique → CG échoue.
- **OC update sans projection sur les bornes** : ρ peut sortir de [ρ_min, 1].
  Toujours clamp après chaque update.

### Référence canonique à reproduire numériquement

- Andreassen et al. 2011, *Struct. Multidisc. Optim.* 43:1
- Compliance MBB finale ≈ 200 (à ±5%) pour 200×100, vol=0.5, p=3, r=2

---

## Phase 2 — Passage 3D + Metal GPU compute

### Objectif

Migrer le solveur vers 3D sur GPU. Le défi n'est pas TO (algorithme inchangé)
mais la maîtrise de Metal pour le FEM creux.

> **Statut (clôturé 2026-06-26)** : fait, mais via une approche **matrix-free**
> (K jamais assemblée) plutôt que CSR — supérieure pour 128³ (mémoire, pas
> d'atomics). Validé : patch 3D, cantilever, CG vs CPU, MBB 3D ; solve 128³ 6.4 s.
> Réserve : opti 128³ complète 16.6 min (Jacobi faible → multigrid Phase 3). Détail :
> `TopOptP2/PHASE_2_REPORT.md`, `../orchestration/handoffs/PHASE_2_TO_3.md`.
> Les mentions "CSR" ci-dessous sont historiques.

### Acquis en sortie de phase

- ✓ Tout l'acquis Phase 1, mais en 3D
- ✓ Solveur CG préconditionné Jacobi (point de départ) sur GPU
- ✓ Assembly K en format CSR sur GPU
- ✓ Multiplication matrice-vecteur creuse sur GPU (SpMV)
- ✓ Calcul d'erreur résiduelle sur GPU
- ✓ Loop CG complète GPU (sans aller-retour CPU sauf pour critère d'arrêt)
- ✓ Visualisation 3D : extraction marching cubes de l'iso ρ=0.5, export STL,
  rendu dans ton SDFViewer
- ✓ Test poutre console 3D : flèche analytique reproduite
- ✓ Test MBB 3D : design symétrique en 3D, treillis cohérent
- ✓ Benchmark : 128³ converge en < 10 minutes sur M-series

### Fragilités potentielles

1. **Convergence CG lente** : Jacobi est un préconditionneur faible. Sur
   128³, attendre ~500-2000 itérations CG par solve. C'est OK en Phase 2,
   à corriger en Phase 3 (multigrid).
2. **Coalescence mémoire** : pattern d'accès aux DOF doit suivre l'ordre
   Morton ou row-major selon l'organisation des kernels. Performance peut
   chuter de 10× si mal fait.
3. **Précision float vs double** : sur GPU, float32 par défaut. Pour CG,
   c'est acceptable en simple précision si bien préconditionné. Vérifier
   que la convergence ne s'arrête pas sur du bruit numérique (résidu
   stagnant > 1e-5).
4. **Tailles de threadgroups Metal** : 256 ou 512 threads est typique.
   Tester plusieurs valeurs, garder ce qui maximise l'occupancy.
5. **Memory pressure** : 128³ × 24 DOF (3 par nœud) × 8 bytes ≈ 200 MB
   juste pour les vecteurs. K creuse : ~1-5 GB selon stencil. Sur 64 GB
   c'est OK, mais surveiller les pics.

### Dette technique nouvelle

- Pas de préconditionnement multigrid (CG basique)
- Pas de multi-grille pour warm-start
- Visualisation : extraction marching cubes peut être lente (Phase 3 améliore)

### Ce qui doit changer en Phase 3

| Composant | Phase 2 | Phase 3 |
|---|---|---|
| Solveur linéaire | CG + Jacobi | CG + multigrid (V-cycle) OU multi-grid warm-start |
| Stratégie TO | Une seule grille fine | Hiérarchie 64³ → 128³ → 256³ |
| Filtre Helmholtz | Rayon en cellules | Rayon physique en mm (mesh-independent) |
| Validation | Convergence sur 128³ | Mesh independence sur plusieurs résolutions |

### Pièges spécifiques Phase 2

- **Race conditions dans l'assembly GPU** : si deux threads écrivent sur le
  même nœud (nœud partagé entre éléments), atomicAdd obligatoire. Sans ça,
  K mal assemblée silencieusement.
- **Précision sur K U = F** : test patch doit passer à 1e-10 en float64,
  1e-6 en float32. Si pire, bug d'assembly.
- **Numérotation Morton vs row-major** : choisir une fois pour toutes,
  noter dans le code, ne plus changer. Conversions répétées = bug source.
- **Memory leak GPU** : sur Metal, oublier d'invalider un MTLBuffer entre
  itérations peut causer croissance mémoire silencieuse. Utiliser autorelease
  pools ou ARC strict.

### Référence canonique

- Aage-Lazarov-Sigmund 2015, *Struct. Multidisc. Optim.* 51:565 — large-scale
  parallel TO. Bien que sur CPU/PETSc, méthodes transposables au GPU.

---

## Phase 3 — Multi-grid uniforme + mesh independence

### Objectif

Construire la hiérarchie de grilles. C'est le saut de "ça marche" à "ça
marche bien et rapidement". Cette phase n'ajoute pas de physique nouvelle
mais rend tout le reste viable industriellement.

### Acquis en sortie de phase

- ✓ Hiérarchie de grilles fonctionnelle (3-4 niveaux)
- ✓ Opérateurs d'interpolation et restriction implémentés et testés
- ✓ Warm-start : ρ niveau N+1 initialisé depuis interpolation de ρ niveau N
- ✓ Filtre Helmholtz avec rayon physique r en mm (indépendant de h)
- ✓ Démonstration de mesh independence : design à 64³ ≡ design à 256³
  (à raffinement près)
- ✓ Optionnel : préconditionneur multigrid V-cycle pour le CG du primal
- ✓ Speedup total : 5-10× sur l'optimisation complète
- ✓ Pipeline JSON → optim multi-grid → STL marche end-to-end
- ✓ Tests : si je change r de 1mm à 2mm, le design change visiblement et
  prévisiblement (features plus grosses)

### Fragilités potentielles

1. **Interpolation non-conservative** : naïvement, ρ̃_fin(x) = ρ_grossier(x)
   ne conserve pas le volume. Utiliser interpolation conservative (moyenne
   pondérée par volume). Sinon dérive de volume entre niveaux.
2. **Initial design grossier mauvais** : si le design 64³ converge vers un
   mauvais minimum local, le warm-start 128³ propage le problème. Diagnostiquer
   par variations de l'init : ρ uniforme 0.5 vs random vs gradient. Tester
   plusieurs init et garder le meilleur.
3. **Continuation des paramètres entre niveaux** : p de SIMP doit-il
   continuer (1→2→3) à chaque niveau ou être figé à 3 sur les niveaux
   fins ? Choix non-trivial, documenté dans Aage 2015.
4. **Multigrid V-cycle qui ne converge pas** : implémentation correcte
   demande de la rigueur (smoother, coarsening, prolongation, restriction).
   Si V-cycle ne marche pas, fallback sur CG+Jacobi+warm-start est OK pour
   cette phase, à reprendre plus tard.

### Dette technique nouvelle

- Pas d'AMR vraie (grille uniforme à chaque niveau)
- Pas encore de contraintes mécaniques (von Mises)
- Pas encore de physique additionnelle (thermique, fluide)

### Ce qui doit changer en Phase 4

| Composant | Phase 3 | Phase 4 |
|---|---|---|
| Physique | Élasticité linéaire seule | Élasticité + conduction thermique |
| Couplage | Aucun | Thermo-élastique (dilatation) |
| Adjoint | Single-block analytique | Multi-block, dérivation rigoureuse |
| Update | OC (suffisant pour compliance) | MMA obligatoire (multi-contraintes) |
| Contraintes | Volume seul | Volume + von Mises + T_max |
| Cas test | MBB structural | Tuyère 2D axi sous pression + flux thermique |

### Pièges spécifiques Phase 3

- **Conservation du volume entre niveaux** : tester explicitement avant et
  après chaque transition. Tolérance 0.01%.
- **Stationnarité du smoother dans V-cycle** : Gauss-Seidel marche mieux
  que Jacobi pour le smoother multigrid. Mais GS est séquentiel et donc
  pénible sur GPU. Alternative : red-black Gauss-Seidel (parallélisable).
- **Choix de r en mm** : trop petit → checkerboards non éliminés ;
  trop grand → features bouchées et perte d'expressivité du design. Règle :
  r ≈ 2-4 × h_min (la plus petite cellule).
- **Tuyère pas encore en domaine axisymétrique** : Phase 3 reste sur géométrie
  cartésienne 3D. Adaptation 2D axi en Phase 4.

### Référence canonique

- Aage-Andreassen-Lazarov 2015 (multigrid + large-scale)
- Lazarov-Sigmund 2011 (filtre Helmholtz)

---

## Phase 4 — Couplage thermo-élastique + contraintes von Mises + 2D axi

### Objectif

Premier couplage physique : ajouter la thermique. Première vraie contrainte
ingénieur : von Mises < σ_yield. Première adaptation aérospatiale : géométrie
2D axisymétrique. C'est ici que ton projet devient spécifique à la propulsion.

### Acquis en sortie de phase

- ✓ Solveur de conduction thermique stationnaire (équation de la chaleur)
  sur la même grille que la mécanique
- ✓ Couplage thermo-élastique faible : T(x) → ε_thermique = α(T-T_ref)
  → contrainte thermique additionnelle
- ✓ Géométrie 2D axisymétrique : intégrales en 2πr, formulation FEM en (r,z)
- ✓ Calcul de la contrainte de von Mises par élément
- ✓ Agrégation p-norm des contraintes (Le-Norato-Bruns 2010) :
  σ_vM_max ≈ (Σ σ_i^p)^(1/p), p=8 ou 16
- ✓ Adjoint multi-block : système (∂R_méca/∂U, ∂R_thermo/∂T) → résolution
  couplée du système adjoint
- ✓ Update MMA (Method of Moving Asymptotes, Svanberg 1987) à la place de OC
- ✓ Cas test : tuyère 2D axi soumise à pression intérieure 80 bar +
  flux thermique pariétal 10 MW/m² → optim épaisseur paroi pour
  von_Mises < σ_yield et T_paroi < T_max
- ✓ Vérification : le design optimal est plus épais au col (max contrainte
  thermique + mécanique) qu'aux extrémités. Si non, bug.

### Fragilités potentielles (NIVEAU CRITIQUE)

1. **Stress singularity en stress-constrained TO** : quand ρ → 0, la
   contrainte σ_vM/ρ → ∞ artificiellement. Solutions : ε-relaxation
   (Duysinx 1998) ou qp-approach (Bruggi 2008). Sans ça, optimiseur
   refuse de mettre du vide (parce que c'est "infini de contrainte").
   **C'est le bug qui te fera perdre 2 semaines si tu le découvres tard.**
2. **Adjoint thermique** : la sensibilité ∂J/∂ρ doit inclure la chaîne
   complète : ρ → K_méca → U, et ρ → K_thermo → T, et T → σ_thermique
   → contrainte. Erreurs très fréquentes ici. Validation OBLIGATOIRE par
   différences finies sur un petit cas (sub-cas 10×10).
3. **p-norm aggregation paramétrage** : p trop petit (4-6) sous-estime
   σ_max ; p trop grand (>20) rend le gradient extrêmement non-lisse.
   Sweet spot 8-12 selon ton cas. Tester.
4. **MMA convergence** : MMA peut osciller si les asymptotes sont mal
   updatées. GCMMA (Svanberg 2002) est plus robuste mais plus complexe.
   Démarrer avec MMA standard, migrer vers GCMMA si oscillations.
5. **Géométrie 2D axi** : ne pas oublier les facteurs 2πr dans les
   intégrales (FEM en coordonnées cylindriques). Singularité à r=0.
   Solution : nœuds à r=ε > 0 ou formulation spéciale axisym.

### Dette technique nouvelle

- Pas encore de fluide
- Couplage thermo-élastique faible (one-way : T → σ, mais pas σ → T)
  est OK pour la plupart des cas mais doit être documenté
- Manufacturing constraints (overhangs LPBF, etc.) non encore traitées

### Ce qui doit changer en Phase 5

| Composant | Phase 4 | Phase 5 |
|---|---|---|
| Physique | Élasticité + thermique | + Stokes incompressible |
| Couplage | Thermo-élastique faible | Stokes + CHT + thermo-élastique |
| Penalization | Aucune côté fluide | Brinkman (α(ρ)u dans Stokes) |
| Adjoint | 2 blocs (méca+thermo) | 3 blocs (méca+thermo+Stokes) |
| Cas test | Tuyère sous P+q | Cooling jacket optimisé |
| Validation | Comparaison Lamé+Bartz | Comparaison à des designs Leap71-like |

### Pièges spécifiques Phase 4

- **Asymétrie de K_couplée** : le couplage thermo-élastique rend la matrice
  globale non-symétrique (T influence σ mais σ pas T). Solveur doit gérer
  ça : passer de CG à BiCGStab ou GMRES.
- **Validation adjoint par DF** : sur 10×10, tester ∂J/∂ρ_i par perturbation
  finie (ε = 1e-6) et comparer à l'adjoint. Doivent matcher à 1e-5 près.
  Sinon adjoint faux.
- **Conditions limites complexes** : pression intérieure → BC de Neumann
  sur la paroi. Flux thermique → BC de Robin. Bien gérer la différence
  entre "imposé" et "calculé".
- **Tolérance MMA** : critères de convergence MMA différents de OC.
  Conservatif initialement, relâcher si trop d'itérations.

### Référence canonique

- Pedersen-Pedersen 2010 *Struct. Multidisc. Optim.* 42:681 (thermo-élastique)
- Le-Norato-Bruns 2010 *Struct. Multidisc. Optim.* 41:605 (stress-constrained)
- Svanberg 1987 *Int. J. Numer. Methods Eng.* 24:359 (MMA original)

---

## Phase 5 — Couplage Stokes + CHT (Conjugate Heat Transfer)

### Objectif

Le livrable phare du projet : TO multiphysique fluide-structure-thermique
sur une géométrie de tuyère/cooling jacket. C'est ce qui te différencie
de 99% des projets étudiants.

### Acquis en sortie de phase

- ✓ Solveur Stokes incompressible avec Brinkman penalization :
  α(ρ) (1-ρ)^q × u dans l'équation de moment, q grand (4-8) pour
  pousser α → ∞ dans le solide
- ✓ Éléments Taylor-Hood (P2 vitesse, P1 pression) ou Q1-Q1 stabilisé (PSPG)
- ✓ Solveur du système Stokes (matrice indéfinie) : MINRES, ou Schur
  complement, ou Uzawa avec préconditionneur
- ✓ Couplage CHT : conduction dans le solide + advection-diffusion dans le
  fluide, continuité de T et q à l'interface (gérée par la grille commune)
- ✓ Adjoint triple-couplé : (∂R_méca, ∂R_thermo, ∂R_Stokes) résolution
  par Newton-Krylov ou par blocs
- ✓ Cas test : design d'une chemise de cooling régénératif pour tuyère
  méthalox 5 kN, contraintes : T_paroi_chaude < T_max, ΔP_cooling < ΔP_max,
  von_Mises < σ_yield. Objectif : minimiser masse.
- ✓ Résultat attendu : canaux à section variable, plus densément espacés
  près du col (zone de pic thermique). Si tu obtiens ça naturellement
  sans encoder explicitement, c'est gagné.

### Fragilités potentielles (NIVEAU TRÈS CRITIQUE)

1. **Brinkman penalization mal calibrée** : si α_max trop faible, le fluide
   "fuit" dans le solide. Si trop grand, conditionnement K explose. Sweet
   spot : α_max = 1e3 à 1e5 selon viscosité. Continuation possible.
2. **Stokes solver** : système indéfini (saddle-point), CG ne marche pas.
   MINRES ou Uzawa. Préconditionnement essentiel (block-diagonal avec
   masse + Laplacien). Sans préconditionnement, convergence absurdement
   lente.
3. **Adjoint triple-couplé** : c'est le point le plus difficile du projet.
   Validation absolument obligatoire par DF sur petit cas. Si DF et adjoint
   ne matchent pas à 1e-3 près, **arrêter et debugger**, ne pas avancer.
4. **Échelles très différentes** : pression Pa, vitesse m/s, température K,
   contrainte MPa. Mauvaise non-dimensionnalisation = matrices mal
   conditionnées (cond > 1e15) = solveurs qui divergent silencieusement.
5. **Coût computationnel** : chaque évaluation du gradient nécessite 3
   solves primaux (méca, thermo, Stokes) + 3 solves adjoints. Sur 128³,
   typiquement 30 minutes par itération TO. 100 itérations = 50 heures.
   Acceptable pour démo, prohibitif pour itération rapide.
6. **Stokes incompressible vs réalité compressible** : à hautes vitesses
   (M > 0.3), incompressibilité invalide. Soit on reste dans le régime
   adapté (cooling channels lents), soit on documente la limitation.

### Dette technique restante

- Pas de Navier-Stokes (Stokes seul = visqueux dominant)
- Pas de turbulence (laminaire seul)
- Pas de transitoires (stationnaire)
- Pas de plasticité (élasticité linéaire)
- Pas de fatigue (statique uniquement)
- Manufacturing constraints simplifiées (pas d'overhang Langelaar complet)

### Référence canonique

- Borrvall-Petersson 2003 *Int. J. Numer. Methods Fluids* 41:77 (Brinkman
  pour TO fluide)
- Dilgen-Dilgen-Fuhrman-Sigmund-Lazarov 2018 *Struct. Multidisc. Optim.*
  57:1905 (TO multiphysique fluide-thermique adjointe)
- Alexandersen-Andreasen 2020 *Fluids* 5(1):29 (review TO fluide)

### Pièges spécifiques Phase 5

- **Inf-sup condition** (Babuška-Brezzi) : choix d'éléments fluide essentiel.
  Mauvais choix = oscillations de pression, design absurde. Taylor-Hood
  safe, Q1-Q1 stabilisé OK avec PSPG.
- **Brinkman gradient discontinu** : ∂α/∂ρ peut être très non-linéaire
  selon la formulation. Tester continuité du gradient à l'interface
  ρ → 1.
- **Décollement de canaux** : optim peut produire des microcanaux qui se
  pincent au point que Brinkman ne pénalise plus assez. Filtre projection
  Heaviside obligatoire (Wang-Lazarov-Sigmund 2011) pour pousser ρ vers
  binaire.
- **Validation difficile** : pas de "MBB beam" canonique pour TO multiphysique.
  Comparaisons : reproduction d'un cas Borrvall-Petersson (channel design)
  + comparaison qualitative au cooling jacket Leap71 sur la même spec.

---

## Phase 6 et au-delà — choix à faire

À ce stade, tu décides selon ton avancement et tes objectifs (industriel
vs recherche). Trois directions principales, chacune justifiée :

### Option A — Industrialisation (recommandée si tu vises recrutement)

- Manufacturing constraints : Langelaar overhang filter pour LPBF, taille
  min feature renforcée
- Robust topology optimization : optimiser sous incertitudes (matériaux,
  charges) — Lazarov-Schevenels-Sigmund 2012
- UQ par polynomial chaos
- Validation expérimentale : si tu peux faire imprimer un échantillon
  optimisé et le tester, **signal recruteur explosif**
- Documentation utilisateur, exemples industriels packagés

### Option B — Recherche (recommandée si tu vises thèse)

- AMR vraie (octree adaptatif p4est ou deal.II)
- Adjoint sur grille adaptative (sujet ouvert)
- Multilevel methods avec préconditionnement multigrid couplé
- Extension Navier-Stokes (RANS) — implique stabilisation, turbulence
- Couplage avec différentiable physics (JAX-FEM intégration)

### Option C — Verticalisation (recommandée si tu veux entreprendre)

- Focus sur un cas d'usage unique : moteur-fusée bipropergol
- Bibliothèque de "PhysicsModules" couvrant tout le système (cf. nos
  discussions précédentes)
- Pipeline complet : spec → optim → STL → fabrication AM → test
- Comparaisons head-to-head avec Noyron, nTop sur les mêmes specs

---

## Méta-règles transversales (à appliquer à toutes les phases)

### Validation obligatoire à chaque phase

1. **Test patch FEM** : champ uniforme reproduit exactement (1e-10 en
   double, 1e-6 en float)
2. **Validation analytique** : au moins un cas avec solution analytique
   connue (poutre console, cylindre sous pression, etc.)
3. **Validation par différences finies du gradient adjoint** : sur un
   sub-cas petit (10×10 à 20×20), comparer ∂J/∂ρ adjoint vs DF, tolérance
   1e-3 à 1e-5 selon la complexité
4. **Reproduction d'un cas canonique de la littérature** (MBB, Borrvall-
   Petersson channel, etc.)
5. **Mesh independence** : raffiner, vérifier que le design converge
   visuellement (à raffinement près)

### Documentation continue

- Chaque phase produit un `PHASE_N_REPORT.md` documentant les choix,
  les validations, les écarts, les limitations
- Le `TRANSITIONS.md` est mis à jour à chaque fin de phase
- Tests unitaires sont ajoutés à chaque module nouveau
- Benchmarks de performance sont conservés (temps d'optim par cas test)

### Signaux de dette technique à surveiller

- Bugs récurrents nécessitant des "patches" plutôt que des fixes propres
- Tests cassés "que je corrigerai plus tard"
- Code dupliqué entre 2D et 3D
- Magic numbers dans le code sans justification
- Fonctions de plus de 100 lignes
- Fichiers de plus de 500 lignes
- Absence de validation par DF du gradient adjoint
- Performance qui régresse silencieusement entre phases

### Drapeaux rouges qui exigent STOP avant phase suivante

- Mesh independence non démontrée
- Gradient adjoint non validé par DF
- Test patch FEM qui ne passe pas
- Cas canonique non reproduit
- Performance > 2× au-delà de la cible documentée
- Compilation avec warnings non résolus

---

## Pour faire bonne figure devant des recruteurs

Tout ce qui suit doit être présent en fin de Phase 5 minimum :

- README clair avec gallery d'images de designs optimisés (avant/après,
  comparaisons résolution, cas industriels)
- Documentation technique détaillée par phase
- Tests automatisés avec coverage > 70%
- Benchmark report : temps d'optim par cas, scaling vs résolution
- Vidéos/GIFs de convergence d'optimisation
- Un cas de validation contre un papier publié (MBB ou Borrvall-Petersson),
  résultats numériques comparables à 5% près
- Un cas industriel (tuyère ou cooling jacket) avec justification physique
  des choix
- Limitations honnêtement documentées (ce que le code ne fait PAS)
- Roadmap claire des prochaines étapes possibles

Le pire défaut chez un projet étudiant : pas de validation rigoureuse,
pas de comparaison à des références publiées. Le recruteur expert sait
identifier ça en 5 minutes. Investis 20% du temps de projet dans la
validation. C'est ce qui fait la différence entre "joli mais douteux"
et "rigoureux et impressionnant".
