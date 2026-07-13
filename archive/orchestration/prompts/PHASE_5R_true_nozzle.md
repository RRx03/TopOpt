# PHASE 5R — Vraie géométrie de tuyère (alésage profilé convergent-divergent)

## Objectif
Un démonstrateur de tuyère avec un **vrai col** : alésage interne `r_in(z)`
convergent-divergent (rétréci au col), paroi optimisée par TO structurelle
(masse-min sous von Mises, pression interne). Corrige la limite du démonstrateur
P4 (`nozzle_axi`, alésage constant). Réutilise la machinerie axisym validée. CPU double.

## Approche : grille axisymétrique à coordonnées MAPPÉES
- `Grid2DAxi`/`AxiQ4Element`/`FEM2DAxi` sont validés (Lamé, ordre 2) mais sur un
  annulaire rectangulaire (a constant). L'élément `AxiQ4Element::stiffness(r_nodes,
  z_nodes, nu)` prend les 4 coordonnées nodales → il gère déjà les éléments
  **distordus** (le Jacobien s'en charge).
- Il suffit donc de fournir des **coordonnées nodales mappées** suivant le contour :
  chaque rangée `j` (en z) s'étend de `r_in(z_j)` à `r_out(z_j)`.
  `r(i,j) = r_in(z_j) + i·(r_out(z_j) − r_in(z_j))/nr`.
- Contour : `r_in(z) = r_throat + K·(z − z_throat)²` (parabolique) OU cloche cosinus ;
  `r_out(z) = r_in(z) + wall_max` (ou r_out constant). Col au milieu (z_throat=H/2).

## Étapes
1. **Support des coordonnées mappées** : soit une variante `Grid2DAxiMapped`
   (stocke r(i,j), z(j)), soit passer les coords nodales à `FEM2DAxi`. Adapter
   `FEM2DAxi::elementCoords` et la BC de pression interne (sur la face `r_in(z)`,
   maintenant profilée) pour utiliser les coords mappées.
2. **SANITY (obligatoire)** : avec un mapping TRIVIAL (r_in=a constant, r_out=b),
   le solveur doit **reproduire le cas Lamé** (erreur σ_θ < 2% à nr=40) — prouve que
   le mapping ne casse pas la correction. Le test axisym existant doit rester vert.
3. **TO tuyère** : domaine = paroi entre r_in(z) et r_out(z). Pression interne sur
   `r_in(z)`, plane strain (u_z=0 sur z=0/H). Masse-min sous von Mises p-norm ≤ σ_lim,
   via MMA (réutilise `AxiStressAdjoint` pour la sensibilité — déjà validé DF 2.7e-9)
   + filtre densité + Heaviside. ~60 itérations.
4. **Sortie** : coupe PNG (r,z) montrant le VRAI col (alésage profilé) + STL révolutionné
   (via `MarchingCubes` ou `STLExporter`) montrant la tuyère 3D avec col.

## Sanity + comparaison (l'oracle)
- Sanity Lamé (mapping trivial) : σ_θ reproduit. C'est le garde-fou de correction.
- TO : masse décroît, contrainte active, quasi-binaire ; **la géométrie a un col
  visible** (contraste avec nozzle_axi). Vérifier épaississement de paroi au col
  (zone de pic de contrainte).

## Livrables
- `Grid2DAxiMapped` (ou extension) + `src/apps/nozzle_profiled.cpp`.
- Test de sanity mapping→Lamé (dans le test axisym ou un nouveau).
- Sorties `output/nozzle_profiled.{png,stl}`.
- Cible Makefile CPU-pure. `make` 0 warning, clean build (LL-010). NE PAS committer.
- Rapporter : sanity Lamé (mapping trivial) OK ?, courbe masse, col visible ?,
  épaississement au col, fichiers.

## Garde-fous
LL-008/009/010. Non-régression (le test axisym rectangulaire reste vert). Attention
au signe/orientation du contour et à la BC de pression sur la face profilée.
