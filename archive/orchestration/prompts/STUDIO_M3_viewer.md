# STUDIO M3 — viewer résultats vtk.js (.vti / .stl)

## Objectif
Étendre `web/` (M2 committé, a251c5e) au jalon M3 : visualiser les résultats du
solveur dans le Studio — la boucle auteur→run→résultat se ferme. Lis d'abord
docs/WEB_MODELER_SPEC.md (§1, §2, §5-M3) et le code web/src/ existant.

## Livrables
1. **Onglet/mode « Résultats »** basculable depuis l'éditeur (l'état d'édition
   est conservé) : zone de chargement (file picker + drag-drop) de `.vti`
   (VTK XML ImageData produit par topopt_run) et `.stl` (binaire, marching cubes).
2. **Rendu .vti** (vtk.js) :
   - liste des champs du fichier (density, vonMises, temperature, speed,
     displacement…), sélection du champ actif ;
   - **iso-surface** à seuil réglable (slider, défaut 0.5 pour density) ;
   - **coupes orthogonales** X/Y/Z avec position réglable et colormap ;
   - colormaps (viridis/coolwarm au minimum) + barre d'échelle min/max ;
   - combinaison utile : iso-surface density + coupe colorée par un autre champ
     (le cas cooling jacket : canaux + température).
3. **Rendu .stl** : géométrie ombrée, orbite.
4. **Critère d'acceptation (cahier des charges §5-M3)** : charger
   `output/cooling_jacket_full/cooling_jacket_full.vti` (produit par le solveur ;
   régénère-le via `./build/topopt_run examples/cooling_jacket_full.topopt.json`
   si absent — seul usage du solveur autorisé, rien n'y est modifié),
   iso-surface density=0.5, coupe + colormap température. Automatise ce qui
   l'est (test de parsing du .vti réel : champs présents, dimensions, plages) ;
   le rendu visuel sera validé par l'utilisateur.
5. **Tests** : parseur .vti sur le fichier réel (nombre de champs, dims 12×12×20,
   plages plausibles), .stl (nombre de triangles), tous les tests M1/M2 verts.

## Contraintes
- Dépendance runtime autorisée : `@kitware/vtk.js` UNIQUEMENT (code-split :
  chargé dynamiquement à l'ouverture du mode Résultats pour ne pas alourdir
  l'éditeur). TS strict 0 erreur ; build et tests verts. Tout sous web/ ;
  AUCUNE modification hors web/ ; aucun commit.
- Si vtk.js s'avère ingérable en TS strict sur un point précis, isole les
  `any` dans un module wrapper unique (web/src/viewer/vtk.ts) et documente-le.

## Rapport final
Fichiers, sorties tests/build, taille des chunks (éditeur vs viewer), ce que
l'utilisateur verra pour cooling_jacket_full, écarts vs cahier des charges.
