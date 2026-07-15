# TopOpt — Optimisation topologique multiphysique sur Apple Silicon

*Synthèse de présentation du projet. Tous les chiffres cités sont mesurés et
proviennent du dépôt (rapports de phase, suite de tests, exemples). Les
limites sont énoncées avec le même soin que les capacités.*

---

## 1. En bref

TopOpt est un solveur d'**optimisation topologique** (topology optimization, TO)
**fluide-structure-thermique** écrit from scratch en **C++20 / Metal**, piloté par
un langage de description de problème en JSON et complété par une interface web
(TopOpt Studio). On décrit un problème — domaine, matériau, physiques, conditions
aux limites, objectif, contraintes — dans un fichier `.topopt.json` ; le solveur
laisse un algorithme de gradient **découvrir la géométrie** (la matière, les trous,
les canaux) ; les résultats s'inspectent dans ParaView ou dans le Studio.

Le démonstrateur phare est une **chemise de refroidissement de tuyère** : dans une
paroi chauffée au col et chargée mécaniquement, l'optimiseur **découvre un réseau de
canaux de refroidissement** — concentrés là où le flux thermique est maximal — en
tenant **quatre contraintes simultanément actives** à convergence (volume de fluide,
température de paroi, perte de charge, contrainte de von Mises). S'y ajoute une
**tuyère axisymétrique à col profilé** (alésage convergent-divergent réel) dont la
paroi est allégée de 61 % sous contrainte de von Mises. La chaîne complète
modéliser → résoudre → visualiser tient dans trois commandes.

---

## 2. Comment ça marche

### La méthode des densités (SIMP)

Le domaine est discrétisé en éléments finis ; chaque élément porte une **densité**
`ρ ∈ [0,1]` (1 = matière, 0 = vide — ou fluide/solide selon la physique). C'est la
variable de design, continue, que l'optimiseur ajuste. Pour éviter les densités
intermédiaires sans sens physique, la loi **SIMP** interpole la rigidité en `ρ^p`
(p = 3) : une densité « grise » coûte sa pleine masse pour une fraction de rigidité,
donc l'optimiseur l'élimine. Un **filtre de Helmholtz** à rayon exprimé en
**millimètres** (pas en cellules) impose une taille minimale de feature — le design
ne dépend plus de la résolution du maillage — et une **projection Heaviside** avec
continuation binarise le résultat (fraction grise finale < 6 % sur le démonstrateur).

### La méthode adjointe : un gradient exact en un solve

Pour faire une descente de gradient, il faut `dJ/dρ` pour **chaque** élément — des
dizaines de milliers à des millions de variables. Par différences finies, cela
coûterait un calcul physique complet *par variable* : impraticable. La **méthode
adjointe** obtient le gradient **complet et exact** au prix d'**un seul solve
supplémentaire** (le problème adjoint), quel que soit le nombre de variables. C'est
elle qui rend l'optimisation topologique possible à grande échelle — et c'est là que
se loge la difficulté mathématique : une erreur de dérivation produit un design qui
*semble* converger mais est faux.

### Le cœur : la cascade triple couplée

Pour la chemise de refroidissement, la physique est une cascade à sens unique :

```
ρ (design) → écoulement Stokes-Brinkman (u, p) → température advectée T (CHT/SUPG)
           → déplacement thermo-élastique U → objectif J
```

Le terme de **Brinkman** pénalise la vitesse dans le solide : la frontière
fluide/solide devient elle-même une variable de design — c'est ce qui permet à
l'optimiseur de **creuser les canaux**. L'adjoint remonte la cascade **à l'envers**
(adjoint élastique → adjoint thermique → adjoint fluide), y compris le terme le plus
délicat : `∂R_t/∂u`, la dépendance du résidu thermique à la vitesse (le fluide
advecte la chaleur), qui couple l'adjoint thermique à l'adjoint fluide. Le gradient
final somme les contributions des trois blocs. Chaque objectif ou contrainte
(compliance, von Mises agrégé, T_max, dissipation visqueuse) réutilise la même
machinerie avec un second membre différent.

### L'optimiseur : MMA

La mise à jour des densités utilise la **Method of Moving Asymptotes** (Svanberg
1987), le standard académique pour la TO multi-contraintes : approximation convexe
séparable, résolution duale (bissection pour une contrainte, Newton projeté pour
plusieurs). L'implémentation est validée contre un optimum analytique (écart
6,4e-14) et recoupée contre une référence indépendante (0,037 % sur le cas MBB).

---

## 3. Le différenciateur : la discipline de validation

Un adjoint faux ne plante pas : il produit silencieusement un mauvais design. La
règle du projet, appliquée sans exception : **aucune optimisation ne tourne sur un
gradient non validé**. Chaque gradient adjoint est comparé, élément par élément,
aux différences finies centrées sur un petit cas, en CPU double précision, **avant
tout usage**. Tolérance exigée : 1e-3 ; précision obtenue : 1e-6 à 1e-9.

### Les sept gates adjoints

| Gate adjoint | Erreur DF max mesurée |
|---|---|
| compliance thermo-élastique (2 blocs) | 1,6e-6 |
| von Mises p-norm, 3D | 1,6e-7 |
| von Mises, axisymétrique | 2,7e-9 |
| **triple-couplé** (Stokes → CHT → élasticité) | **2,1e-7** |
| dissipation visqueuse (TO fluide) | 7,2e-7 |
| température de paroi T_max | 7,5e-8 |
| von Mises à travers la cascade triple | 8,0e-7 |

### Oracles analytiques et littérature

Chaque solveur physique est vérifié contre une solution exacte avant d'entrer dans
la boucle : patch test FEM (précision machine), cylindre de Lamé (axisymétrique,
ordre 2), écoulement de Poiseuille, Darcy-Brinkman (profil en cosh, 1,2e-3),
advection-diffusion (couche limite exponentielle, avec démonstration du piège de
Péclet : sans stabilisation SUPG le champ oscille). Côté littérature, le **diffuseur
de Borrvall-Petersson (2003)** — le cas canonique de la TO fluide — est reproduit
qualitativement (dissipation 97 % meilleure que le design uniforme). L'ensemble
(oracles + gates + non-régression) tourne en une commande : `make test_cpu`,
17 binaires de test.

### La politique d'honnêteté

Le projet documente ses écarts au lieu de les masquer. Exemples concrets, tirés de
l'historique du dépôt :

- **La « tuyère » de Phase 4 n'en était pas une.** Le démonstrateur initial était
  une paroi annulaire à alésage constant, où le « col » n'était que le pic de la
  charge de pression. Le rapport de phase l'affiche en tête de section (« ce N'EST
  PAS une vraie tuyère ») et un commit dédié corrige la communication :
  `docs(phase4): honest correction — nozzle demo is constant-bore wall (not a true
  nozzle)`. La vraie géométrie à alésage convergent-divergent a été construite
  ensuite, sur grille mappée validée par reproduction de Lamé.
- **Le conflit physique T_max / von Mises est documenté, pas contourné.** Sur le
  cooling jacket complet, refroidir davantage exige des canaux près de la source
  chaude, qui amincissent les ligaments porteurs et font monter la contrainte : un
  couple de bornes trop serré est physiquement inatteignable. Le compromis retenu,
  les valeurs « libres » sondées et les essais qui ont établi l'infaisabilité sont
  consignés dans l'exemple lui-même (`examples/cooling_jacket_full.topopt.json`).
- **La p-norm n'est pas le maximum.** La contrainte de von Mises agrégée (p-norm,
  P = 8) approxime le max ; un P trop faible le sous-estime — c'est écrit dans la
  documentation, et le driver imprime le **vrai maximum** de von Mises à côté de la
  valeur agrégée pour que l'écart reste visible à chaque run.
- **Un objectif de perf raté est annoncé comme tel.** Le multigrid visait un
  speedup 5-10× ; il livre 2,4× (le warm-start seul, sans préconditionneur V-cycle,
  différé et documenté) — suffisant pour la cible fonctionnelle « 128³ en moins de
  10 minutes », et dit ainsi.

---

## 4. Positionnement : ce qui est standard, ce qui est plus rare

*Section écrite avec prudence : ce projet est un démonstrateur d'étudiant, pas un
produit ; les comparaisons portent sur le périmètre des méthodes, jamais sur une
prétention de supériorité.*

### Le socle académique (standard, appliqué proprement)

Les briques de méthode sont l'état de l'art académique établi : SIMP
(Bendsøe-Sigmund), filtre de Helmholtz (Lazarov-Sigmund 2011), projection Heaviside,
MMA (Svanberg), relaxation qp et p-norm pour le stress, Brinkman pour la TO fluide
(Borrvall-Petersson 2003). Les références du domaine sont les codes pédagogiques de
Sigmund et du groupe [TopOpt de DTU](https://www.topopt.mek.dtu.dk/) (le
[code 88 lignes](https://www.topopt.mek.dtu.dk/apps-and-software) MATLAB,
Andreassen et al. 2011) et, pour la grande échelle, leur framework
[TopOpt_in_PETSc](https://github.com/topopt/TopOpt_in_PETSc) (C++/MPI parallèle,
compliance élastique 3D) — jusqu'au « giga-voxel » d'une aile complète (Aage et al.,
Nature 2017) sur supercalculateur. Pour l'adjoint, la référence d'automatisation est
[dolfin-adjoint/pyadjoint](https://dolfin-adjoint.github.io/dolfin-adjoint/), qui
dérive automatiquement l'adjoint discret d'un modèle FEniCS/Firedrake écrit en
Python. TopOpt applique ces méthodes telles quelles ; il n'y a pas d'innovation
méthodologique revendiquée sur ce socle.

### Les outils commerciaux

[Altair OptiStruct](https://altair.com/optistruct) est la référence industrielle de
la TO structurelle (SIMP, contraintes de stress, réponses thermiques et TO
thermo-structurelle couplée). [nTop](https://www.ntop.com/) est centré sur la
modélisation implicite et le field-driven design (lattices pilotées par des champs,
TO intégrée). [Autodesk Fusion](https://www.autodesk.com/products/fusion-360/blog/topology-optimization-and-autodesk-fusion/)
propose du generative design cloud fondé sur la TO. La TO structurelle y est mature
et industrialisée à une échelle sans commune mesure avec ce projet. En revanche,
l'optimisation topologique **fluide-thermique-structurelle couplée** — où
l'écoulement, l'advection de chaleur et la réponse mécanique sont différenciés
ensemble dans la boucle — reste essentiellement un sujet de recherche (p. ex. Dilgen
et al. 2018) et est rare dans les offres commerciales.

### Leap71 / Noyron : une approche différente, pas concurrente

[Leap71](https://leap71.com/tech/) développe Noyron, un modèle d'« ingénierie
computationnelle » : les ingénieurs **encodent la logique de conception** (règles,
physique, contraintes de fabrication) en code, et le modèle **génère** la géométrie
via le noyau [PicoGK](https://leap71.com/picogk/) — avec des résultats remarquables
([moteur-fusée kerolox de 5 kN généré sans CAD et testé au banc en 2024](https://leap71.com/2024/06/18/leap-71-hot-fires-3d-printed-liquid-fuel-rocket-engine-designed-through-noyron-computational-model/)).
C'est de la **géométrie procédurale pilotée par la connaissance** : l'humain encode
la forme de la solution. L'optimisation topologique fait l'inverse : personne
n'encode la topologie, un gradient la **découvre**. Les deux approches sont
complémentaires — on peut imaginer une TO qui alimente des règles procédurales, ou
l'inverse — et ce projet ne prétend pas couvrir ce que fait Noyron (ni
réciproquement).

### Ce qui est plus rare ici

- Une **cascade triple adjointe** (Stokes-Brinkman → CHT → thermo-élasticité,
  terme d'advection compris) **validée par différences finies de bout en bout**,
  avec quatre contraintes multiphysiques simultanément actives sur un cas concret.
- Le solveur élastique 3D **matrix-free sur GPU Metal**, sur une machine grand
  public (Apple Silicon, mémoire unifiée) — la grande échelle académique vit
  plutôt sur clusters MPI/CUDA.
- Une **chaîne outillée complète** à cette échelle de projet : langage JSON
  documenté et rétro-compatible → Studio web (édition 3D des BCs, visualisation
  vtk.js, lancement local ou distant des runs) → sorties ParaView/STL.

---

## 5. Comment le projet a été construit

Le développement a suivi des **phases à gates bloquants**, chacune close par un
rapport chiffré (archivé dans `archive/reports/`) :

1. **P1** — TO 2D compliance sur CPU (base de référence).
2. **P2** — passage 3D + GPU Metal : élasticité matrix-free (aucune matrice
   assemblée), CG préconditionné Jacobi, validation patch test / cantilever.
3. **P3** — multigrid warm-start (128³ : 16,6 → 7,0 min) + filtre à rayon physique
   (mm) : indépendance au maillage démontrée à 32³/64³/128³.
4. **P4** — thermo-élasticité, contrainte de von Mises (relaxation qp + p-norm),
   MMA, FEM axisymétrique : premiers adjoints multi-blocs, 5 gates DF/oracles.
5. **P5** — Stokes-Brinkman, CHT/SUPG, **adjoint triple-couplé** (le gate le plus
   dur), démonstrateur cooling jacket, puis contraintes T_max/ΔP/von Mises,
   marching cubes, tuyère profilée, reproduction Borrvall-Petersson.
6. **Outillage** — langage d'entrée JSON (v1 structurel → v2 thermo-élastique →
   v3 fluide-thermique, rétro-compatible), driver `topopt_run`, TopOpt Studio.

La règle transverse : **chaque gradient est validé par différences finies avant
d'être utilisé**, chaque solveur contre un oracle analytique, et chaque phase se
termine par la mise à jour d'un registre de leçons apprises (les pièges numériques
réels — `E_min` vs CG float32, planchers d'arrondi en validation DF, conservation
de masse à travers un saut de Brinkman — y sont consignés avec leur diagnostic).

**Développement assisté par IA, sous spécifications.** Le code a été développé avec
des agents IA orchestrés : chaque brique a fait l'objet d'une spécification écrite
préalable (conservées dans `archive/orchestration/` — briefs de phase, specs de
gate, handoffs entre phases), l'implémentation a été déléguée, et **la validation
est restée systématiquement indépendante de la génération** : oracles analytiques
et différences finies non négociables, revue et arbitrages d'architecture (choix
des éléments, des solveurs, des relaxations) par l'auteur, qui a appris la TO
adjointe en parallèle et documenté chaque décision (ADR). C'est un choix de méthode
assumé et vérifiable dans l'historique : spécifier précisément, déléguer
l'implémentation, vérifier par des oracles indépendants — les gates chiffrés
ci-dessus sont précisément ce qui rend ce mode de travail sûr.

---

## 6. Capacités actuelles et limites

### Capacités (par dispatch du driver JSON)

| Dispatch | Physique | Objectif / contraintes | Résultat mesuré |
|---|---|---|---|
| **structurel** (GPU) | élasticité 3D, CG matrix-free Metal, multigrid | compliance + volume | MBB 128³ (6,4 M DOF) : optimisation complète en 7,0 min (M4 Max) ; MBB JSON : C = 18,5216, reproduit par le Studio |
| **thermo-élastique** (CPU) | conduction → élasticité (adjoint 2 blocs) | masse s.t. von Mises | `bracket_thermo` 24×8×8 : contrainte active à convergence |
| **fluide-thermique** (CPU) | Stokes-Brinkman → CHT → élasticité (adjoint triple) | compliance s.t. jusqu'à 4 contraintes (volume, T_max, dissipation/ΔP, von Mises) | cooling jacket 12×12×20 (`examples/cooling_jacket.topopt.json`) : J −77 %, gris 7,0 %, densité de coolant au col 2,6× les extrémités ; version 4 contraintes : toutes actives à convergence |
| **axisymétrique** (CPU) | Q4 (r,z), alésage profilé convergent-divergent | masse s.t. von Mises | `nozzle_profiled` 24×80 : masse −61 %, épaississement au col 2,42× |

Sorties : volumes `.vti` (densité, von Mises, température, vitesse, déplacement)
pour ParaView, géométrie `.stl` par marching cubes (oracle sphère : 0,06 %).
Le tout pilotable depuis TopOpt Studio (three.js + vtk.js), y compris le lancement
de runs sur une machine distante.

### Limites (chacune avec sa raison et son chemin d'extension)

| Limite | Raison | Extension |
|---|---|---|
| **Stokes, pas Navier-Stokes** : écoulements lents/visqueux uniquement (pas d'inertie) | linéarité = adjoint exact plus simple, et régime pertinent pour des canaux fins | ajouter le terme convectif (Newton) et différencier l'adjoint correspondant |
| **Couplage one-way** (fluide → thermique → structure) : pas de retour de la déformation sur l'écoulement | cascade validable bloc par bloc ; retour négligeable aux petites déformations | itérations de point fixe ou FSI complet, avec re-validation DF |
| **Ni turbulence, ni convection naturelle** | hors du périmètre Stokes/CHT stationnaire actuel | modèles RANS différenciés / terme de Boussinesq — travail de recherche substantiel |
| **Multiphysique en CPU double précision, grilles modestes** (cooling jacket : 12×12×20) | choix assumé « correctness d'abord » : les oracles exigent le double | portage GPU float32 des solveurs et adjoints multiphysiques, re-validé contre le chemin CPU |
| **Élasticité linéaire**, petites déformations, propriétés indépendantes de T | périmètre du démonstrateur | hyperélasticité / plasticité = nouveaux adjoints à dériver et gater |
| **Optima locaux** : la TO par gradient ne garantit pas l'optimum global ; MMA classique n'est pas globalement convergent | nature du problème (non convexe) ; MMA standard suffit sur les cas traités | continuations déjà en place (p, β) ; GCMMA en repli documenté si oscillations |
| **Q1-Q1 PSPG** : conservation de masse imparfaite à travers un fort saut de Brinkman | cohérence avec la grille structurée matrix-free | α_max modéré (calibré, fuite 0,47 %) ; Taylor-Hood si besoin de plus |
| **Adjoint SUPG non différencié** : gates validés à Péclet modéré | la dérivée du terme de stabilisation est un chantier à part | différencier SUPG et re-gater pour le haut Péclet |
| **Géométries** : boîtes voxel + alésage axisymétrique mappé, pas de maillage CAD général | grille structurée = clé du matrix-free GPU | domaines masqués, immersed boundaries, ou piste non structurée |
| **p-norm ≈ max** : la contrainte agrégée approxime le maximum de von Mises | agrégation différentiable obligatoire | le vrai max est rapporté à chaque run ; continuation de P possible |

---

## 7. Ce que ce projet démontre

Sur le fond : la **méthode adjointe appliquée à une physique couplée à trois blocs**,
dérivée à la main et vérifiée numériquement à 1e-7 près — le point précis où la
plupart des implémentations de TO multiphysique échouent silencieusement. Sur la
forme : un projet mené comme un produit — phases gatées, non-régression exécutable
(`make test_cpu`), langage d'entrée versionné, outillage complet, historique
honnête où les corrections et les objectifs manqués sont écrits noir sur blanc.
Et une compétence d'ingénierie contemporaine : orchestrer un développement assisté
par IA **sans jamais déléguer la vérification**.

### Pour aller plus loin

- [`docs/THEORY.md`](THEORY.md) — la théorie complète : hypothèses et sacrifices
  par physique, dérivation adjointe, MMA, les 7 gates, bibliographie.
- [`docs/TECHNICAL.md`](TECHNICAL.md) — le fonctionnement informatique :
  matrix-free Metal, cascade adjointe implémentée, serveur de run, Studio.
- [`docs/USER_GUIDE.md`](USER_GUIDE.md) — le guide d'utilisation : référence
  JSON champ par champ, conventions pièges, exemples commentés, ParaView.
- [`docs/INPUT_LANGUAGE.md`](INPUT_LANGUAGE.md) — le schéma `.topopt.json` :
  domaine, matériaux, physiques, BCs, objectifs, contraintes, sorties.
- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) — modules et flux de données ;
  [`docs/WEB_MODELER_SPEC.md`](WEB_MODELER_SPEC.md) — le Studio et sa feuille de route.
- [`archive/reports/`](../archive/reports/) — les rapports de phase chiffrés ;
  [`README.md`](../README.md) — build et prise en main (3 commandes).

*Auteur : étudiant à ISAE-SUPAERO. Le dépôt se construit et se valide en local
(`make -j && make test_cpu`) sur tout Mac Apple Silicon, dépendances vendorisées.*
