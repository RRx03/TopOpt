# PHASE 5R — Marching cubes : iso-surface lisse (STL gallery)

## Objectif
Extraction d'iso-surface **lisse** (marching cubes standard) d'un champ scalaire 3D
sur `Grid3D` → STL, remplaçant la surface voxel « en escalier » de `STLExporter`.
Améliore tous les rendus (gallery recruteur). Validé quantitativement sur une sphère.

## Formulation
- Marching cubes classique (tables 256 cas de Paul Bourke : `edgeTable[256]`,
  `triTable[256][16]`). Pour chaque cellule, index 8 bits selon les sommets
  au-dessus/sous l'iso, interpolation LINÉAIRE des sommets de triangle le long des
  arêtes (`v = v0 + t(v1−v0)`, t=(iso−f0)/(f1−f0)) → surface lisse.
- Champ scalaire aux **nœuds** (pas aux cellules). Si le champ TO est par élément,
  interpoler élément→nœud d'abord (moyenne des éléments incidents).

## Validation (oracle sphère)
- Champ f(x)=R−|x−c| (SDF sphère) échantillonné aux nœuds, iso=0.
- Extraire → mesurer **aire** de la surface et **volume** englobé (somme des
  tétraèdres signés / divergence). **PASS si** aire ≈ 4πR² et volume ≈ (4/3)πR³
  à quelques % (erreur de discrétisation, décroît si on raffine — vérifier).
- **Watertight** : chaque arête partagée par exactement 2 triangles (ou compter
  les arêtes de bord = 0).

## Livrables
- `src/io/MarchingCubes.{hpp,cpp}` : `extract(field, grid, iso) -> triangles` +
  `writeSTL(path, ...)`. (Réutilise le pattern binaire STL de `STLExporter`.)
- `tests/test_marching_cubes.cpp` (CPU pur) : sphère (aire/volume/convergence + watertight).
- Optionnel : régénérer un STL lisse d'un résultat TO existant (ex. cooling_jacket
  densité révolutionnée ou champ 3D) pour comparaison visuelle voxel vs lisse.
- Cible Makefile CPU-pure. `make` 0 warning, clean build (LL-010). NE PAS committer.
- Rapporter : erreur aire/volume sphère + convergence, watertight (arêtes de bord),
  fichiers.

## Garde-fous
LL-010 (clean build). Non-régression. Attention à l'ordre des sommets (normales
sortantes cohérentes) et aux cas ambigus (la table standard les gère).
