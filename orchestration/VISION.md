# VISION.md — Pourquoi TopOpt

*Document de positionnement. Lecture en début de Phase 1 de chaque grande
session pour rappel du cap. Mis à jour seulement si le positionnement stratégique
change.*

---

## 1. Le problème

La conception aérospatiale moderne — tuyères de fusée, chambres de combustion
régénératives, échangeurs, dissipateurs — est intrinsèquement **multiphysique** :
la structure doit tenir des pressions de plusieurs dizaines de bar, évacuer des
flux thermiques de l'ordre de 10 MW/m² au col, et souvent canaliser un fluide de
refroidissement, le tout en minimisant la masse. Ces trois physiques
(élasticité, thermique, fluide) sont couplées et antagonistes : épaissir une
paroi pour tenir la contrainte mécanique dégrade l'échange thermique ; ouvrir un
canal de refroidissement affaiblit la structure.

La conception manuelle ou paramétrique de ces objets atteint vite ses limites :
l'espace de design est trop grand, les compromis trop fins. La **topology
optimization (TO) adjointe** répond exactement à ce problème — elle laisse
l'algorithme découvrir la distribution de matière optimale sous contraintes
multiphysiques, en calculant le gradient exact de la fonction objectif par
rapport à des millions de variables de design via la **méthode adjointe**.

Mais les outils existants ne couvrent pas ce besoin de bout en bout : soit ils
font de la TO purement structurelle, soit de la simulation sans optimisation,
soit de la recherche non industrialisable.

---

## 2. L'état de l'art

| Acteur | Paradigme | Force | Limite pour notre besoin |
|---|---|---|---|
| **Leap71 / Noyron** | KBE procédural sur SDF voxel (computational engineering models) | Génère des moteurs-fusées complets imprimables ; spectaculaire industriellement | **Pas de TO** : c'est du *knowledge-based engineering* paramétrique, pas de l'optimisation par gradient. Le designer encode les règles, l'outil ne *découvre* pas la topologie. |
| **nTop (nTopology)** | Implicit modeling + champs, simulation via solveurs externes | Modélisation implicite robuste, lattices, intégration industrielle | TO structurelle ; multiphysique couplée limitée ; propriétaire, boîte noire. |
| **Altair OptiStruct / Inspire** | TO structurelle industrielle (SIMP, éléments finis) | Mature, validé, standard industrie structure | **Structurel surtout** : couplage thermo-fluide-élastique avec adjoint multiphysique non couvert. |
| **dolfin-adjoint / Firedrake** | TO recherche FEM en Python, adjoint automatique (UFL) | Très flexible, adjoint exact automatique, multiphysique possible | **Recherche** : Python, performance et industrialisation limitées, pas un démonstrateur produit. |
| **JAX-FEM / differentiable physics** | FEM différentiable en Python (autodiff) | Élégant, gradients automatiques | Jeune, performance GPU variable, écosystème Python recherche. |
| **topoptFoam (OpenFOAM)** | TO fluide dans OpenFOAM | TO fluide dans un solveur CFD établi | Centré fluide ; couplage structure-thermique-fluide unifié non natif. |

**Constat** : personne ne propose un démonstrateur **TO adjointe multiphysique
fluide-structure-thermique**, performant, en code système (non-Python), pensé
pour l'aérospatial. C'est l'espace que TopOpt occupe.

---

## 3. Le positionnement de TopOpt

TopOpt est un **solveur de topology optimization adjointe multiphysique
(fluide-structure-thermique) en C++23/Metal sur Apple Silicon**, à vocation de
démonstrateur industriel pour la conception de tuyères et de chemises de
refroidissement régénératif.

Quatre différenciateurs assumés :

1. **Vraie TO, pas du KBE.** Contrairement à Noyron, on ne code pas les règles de
   design : on définit un objectif, des contraintes, et l'algorithme *découvre*
   la topologie par descente de gradient adjoint. C'est ce que Leap71 n'a pas
   fait et que peu d'acteurs intègrent à ce niveau.
2. **Multiphysique couplée**, pas seulement structurel. L'aboutissement (Phase 5)
   couple élasticité + thermique + Stokes avec un **adjoint triple-couplé** —
   le cœur scientifique différenciant.
3. **GPU Apple Silicon natif**, pas un portage. FEM creux et solveurs itératifs
   en Metal compute sur mémoire unifiée, exploitant la plateforme de bout en bout.
4. **C++ industriel**, pas Python recherche. Code système, performant,
   maintenable, défendable comme base d'un outil réel.

---

## 4. La roadmap en 5 phases (vue d'ensemble)

Le projet monte en complexité physique par étapes, chacune validée avant la
suivante (détails : `orchestration/ROADMAP.md`).

1. **Phase 1 — TO structurelle 2D (SIMP, OC, adjoint compliance).** *Terminée.* La fondation : on maîtrise SIMP, le filtre, le gradient adjoint sur le cas canonique MBB.
2. **Phase 2 — Passage 3D + Metal GPU.** *Fondation faite, cœur à terminer.* Le défi est la maîtrise de Metal pour le FEM creux à grande échelle.
3. **Phase 3 — Multi-grid + mesh independence.** Rend le solveur viable industriellement (vitesse + indépendance au maillage).
4. **Phase 4 — Thermo-élastique + von Mises + 2D axi.** Première multiphysique réelle ; premières contraintes ingénieur ; géométrie de tuyère.
5. **Phase 5 — Stokes + CHT (multiphysique complète).** Le livrable phare : cooling jacket régénératif optimisé par adjoint triple-couplé.

Phase 6 = extension à arbitrer (industrialisation / recherche / verticalisation).

---

## 5. Les risques et leur mitigation

| Risque | Gravité | Mitigation |
|---|---|---|
| **Adjoint multi-bloc faux** (Phase 4-5) : converge en apparence vers un design aberrant | Critique | Validation par différences finies OBLIGATOIRE sur petit cas (10×10), tolérance 1e-5, avant tout run grande taille. |
| **Brinkman penalization instable** (Phase 5) : fuite fluide ou conditionnement explosif | Élevée | Calibration α_max ∈ [1e3, 1e5], continuation progressive, tests de non-fuite. |
| **Saddle-point Stokes** mal résolu (inf-sup) | Élevée | Éléments stables (Taylor-Hood) ou stabilisation PSPG ; solveur MINRES/Uzawa préconditionné. |
| **Coût computationnel** (Phase 5) : ~30 min/itération sur 128³ | Moyenne | Multi-grid (Phase 3) ; démos à résolution réduite ; documenter les limites. |
| **Mauvaise non-dimensionnalisation** : matrices mal conditionnées | Élevée | Échelles de référence définies avant assemblage ; résidu non-dim contrôlé. |
| **Mesh dependence** : design dépend du maillage | Moyenne | Filtre Helmholtz à rayon physique (mm) dès Phase 3 ; démonstration explicite. |
| **Dérive doc↔code** | Moyenne | Le code fait foi ; documentation au fil de l'eau ; analyse archéologique de référence. |

Principe transversal de mitigation : **valider rigoureusement chaque brique avant
d'empiler la suivante.** La dette technique en TO multiphysique est exponentielle.

---

## 6. Le signal recruteur attendu en fin de Phase 5

Un **démonstrateur capable de produire un design de chemise de refroidissement
régénératif défendable industriellement** : canaux à section variable, plus
densément groupés près du col (zone de pic thermique), obtenus *sans* les encoder
explicitement — purement par optimisation adjointe sous contraintes (T_paroi,
ΔP, von Mises), objectif masse.

Accompagné de ce qui sépare un projet rigoureux d'une démo douteuse :
- validation numérique contre un cas publié (Borrvall-Petersson) à ~5 % près ;
- gradients adjoints validés par différences finies ;
- mesh independence démontrée ;
- benchmarks de performance mesurés (pas estimés) ;
- limitations honnêtement documentées.

C'est ce dossier — vraie TO multiphysique, validée, performante, sur une plateforme
moderne — qui constitue le signal fort face à des recruteurs industriels
aérospatiaux.
