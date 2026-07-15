# STUDIO M2 — import round-trip + presets + validation (matrice de compatibilité)

## Objectif
Étendre `web/` (M1 committé, b3afcda) au jalon M2 du cahier des charges
`docs/WEB_MODELER_SPEC.md` : import `.topopt.json`, galerie des 6 presets du
repo, validation continue par matrice de compatibilité. Lis d'abord le cahier
des charges (§2, §4, §5) et le code M1 (web/src/spec, state.ts, panels/).

## Livrables
1. **Import** : bouton (file picker + drag-drop sur la fenêtre) → `specFromJson`
   → remplace l'état du Store → scène et panneaux se resynchronisent (BCs,
   glyphes, domaine). Les entrées non éditables en M1 (thermal/flow/pressure,
   physiques v2/v3, dim axi) doivent être **préservées à l'export** (round-trip
   sans perte) et listées en lecture seule dans l'UI (section « BCs avancées
   (lecture seule) » + bandeau « physique fluide-thermique : édition V2 »).
   L'utilisateur doit pouvoir importer cooling_jacket_full, changer max_iter,
   ré-exporter, et n'avoir QUE ce delta.
2. **Presets** : les 6 exemples du repo embarqués (copiés dans
   web/src/presets/ à la build par import JSON statique — pas de fetch),
   galerie dans l'UI (nom + physique + une ligne de description), chargement
   en un clic. Source de vérité : les fichiers de `examples/` — un test
   vérifie que chaque preset embarqué est identique au fichier du repo.
3. **Matrice de compatibilité** (§4 du cahier des charges) dans
   web/src/spec/compat.ts : physics (+dim) → objectifs et types de contraintes
   admissibles. Appliquée : (a) les selects de l'UI n'offrent que l'admissible ;
   (b) à l'import d'un spec non-conforme, avertissement non-bloquant listant
   les écarts ; (c) à l'export, erreurs bloquantes affichées (ex. contrainte
   vonmises sans physique elastic).
4. **Panneau optimize généralisé** : liste de contraintes typées (volume, tmax,
   dissipation, vonmises avec max/max_rel) filtrée par la matrice — en édition
   pour les physiques 3d ; l'UI n'expose toujours la CRÉATION de BCs que pour
   l'élastique (thermal/flow restent lecture seule, V2 de l'éditeur 3D).
5. **Tests (vitest)** : round-trip des 6 exemples (import → export → deep-equal
   sémantique à défauts près) ; presets = fichiers repo ; compat (cas admis +
   cas rejetés) ; le golden M1 reste vert.

## Contraintes
- Tout sous web/ ; aucune nouvelle dépendance runtime ; TS strict 0 erreur ;
  `npm run build` et `npm run test` verts. NE PAS committer.
- Attention au piège de sérialisation : specToJson n'émet que le non-défaut —
  le round-trip doit préserver TOUS les champs des 6 exemples (dont
  body_force/drive, nozzle, max_rel, heaviside).

## Rapport final
Fichiers, sorties tests/build, ce que l'UI montre pour chaque preset importé,
écarts vs cahier des charges.
