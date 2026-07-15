# STUDIO M1 — éditeur box + BCs élastiques + export .topopt.json

## Objectif
Créer `web/` (TopOpt Studio, Vite + TypeScript + three.js, app 100 % statique)
couvrant le jalon M1 du cahier des charges `docs/WEB_MODELER_SPEC.md` (§2 MVP,
§3 choix techniques, §4 contrat). Lis le cahier des charges ET
`docs/INPUT_LANGUAGE.md` ET `src/io/ProblemSpec.hpp` (source de vérité du
schéma) avant d'écrire.

## Livrables
1. `web/` : Vite + TS strict + three.js. Panneaux en vanilla + lil-gui (pas de
   framework). Arborescence du §8 du cahier des charges.
2. `web/src/spec/` : type TS `ProblemSpec` MIROIR EXACT de ProblemSpec.hpp
   (champs, défauts identiques), sérialisation .topopt.json (n'émettre que les
   champs non-défaut est acceptable ; le JSON émis doit être accepté par le
   parseur C++), et défauts par preset.
3. Éditeur 3D :
   - box domaine depuis grid/size_mm, wireframe + grille au sol, axes annotés ;
   - picking des 6 faces (raycaster, surbrillance survol, sélection cliquée) ;
   - arêtes : sélection de 2 faces adjacentes (l'intersection, comme le
     sélecteur `edge:"x-,y+"` du contrat) ;
   - sur une sélection : ajout d'un appui (`fixed`, dof x/y/z/all) ou d'une
     charge (`loads`, dof + valeur totale) ;
   - glyphes : cônes/flèches pour les charges, symboles d'encastrement pour les
     appuis, couleur par type ; liste des BCs avec suppression.
4. Panneaux : meta (name), domain (grid, size_mm), material (E0, Emin, nu,
   penal), filter (radius_mm, heaviside beta list, eta), optimize (objective
   compliance|mass, contrainte volume max, optimizer, max_iter), output (dir).
   Physique M1 : `["elastic"]` seul (le reste V2 — ne pas l'exposer).
5. Export : bouton → téléchargement `<name>.topopt.json` + aperçu du JSON dans
   un panneau (copiable).
6. Golden test (vitest) : construire par l'API interne l'état correspondant à
   `examples/mbb3d.topopt.json` (mêmes sélecteurs edge/corner) → l'export doit
   être sémantiquement identique au fichier exemple (parse des deux, deep-equal
   à défauts près). C'est le critère M1 automatisable ; le test « à la souris »
   sera fait par l'utilisateur.

## Contraintes
- Aucune dépendance runtime au-delà de three.js + lil-gui. TS strict, 0 erreur
  `tsc`, 0 warning de build. `npm run dev` / `build` / `test` fonctionnels.
- Ne touche à RIEN hors de `web/` (le solveur C++ est figé pour ce chantier).
- Esthétique sobre type outil d'ingénierie (fond sombre, accents monochromes) ;
  pas de bibliothèque UI.
- Conventions affichées (cahier des charges §4) : force totale distribuée,
  unités mm.
- NE PAS committer.

## Rapport final
Structure du code, commandes (dev/build/test), sortie du golden test, sortie
`npm run build`, captures d'éventuelles limites/écarts vs cahier des charges.
