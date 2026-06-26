# Prompt initial Phase 2 — Claude Code

À coller dans une session Claude Code à la racine du projet TopOpt 
(ou en continuation de la session existante si tu as gardé le contexte).

---

## CONTEXTE

Démarrage de Phase 2 du projet TopOpt. La Phase 1 (TO structurelle 2D 
SIMP, MBB beam) est terminée et validée. Tous les détails sont dans 
`PHASE_1_TO_PHASE_2_HANDOFF.md` à la racine du projet. Lis ce document 
EN PREMIER avant toute action.

Le `CLAUDE.md` existant doit être mis à jour pour refléter Phase 2 
(je te demanderai cette mise à jour explicitement).

## OBJECTIF DE LA SESSION

Démarrer Phase 2 : passage du solveur en 3D avec exécution GPU Metal.
L'algorithme TO reste identique (SIMP + OC + filtre Helmholtz + gradient 
adjoint analytique pour compliance), seules la dimension et la backend 
de calcul changent.

## PRIORITÉS DE CETTE SESSION

Première session Phase 2, focus restreint :

1. Mise à jour du CLAUDE.md pour refléter le contexte Phase 2
2. Vendoring de metal-cpp dans third_party/metal-cpp/
3. Mise à jour du Makefile pour link Metal/Foundation/QuartzCore
4. Création du module MetalContext (initialisation device, queue, capacités)
5. Hello-world Metal : kernel compute trivial qui additionne deux vecteurs 
   MTLBuffer<float> de 1M éléments. Test : la somme matche un calcul CPU 
   à 1e-6 près.

C'EST TOUT pour cette session. On ne touche PAS encore à H8, à 
l'assembly 3D, au CG. Une marche à la fois.

## VALIDATION DE FIN DE SESSION

À la fin de cette session, je veux pouvoir :
- `make` build sans warning
- `./build/test_metal_hello` lance le kernel d'addition vectorielle GPU
- Sortie : "GPU sum matches CPU sum within tolerance 1e-6" ou équivalent
- Le projet ouvre sur la device Metal par défaut (M-series), affiche 
  son nom, sa famille (GPU family), sa mémoire disponible

Si tout ça passe, on a la fondation Metal. Les sessions suivantes 
construiront dessus.

## CONTRAINTES TECHNIQUES NON-NÉGOCIABLES

- Continuité avec Phase 1 : pas de refactor de l'existant 2D, juste ajouts
- metal-cpp officiel d'Apple, single-header download (pas via package manager)
- C++23, clang sur macOS Apple Silicon
- Précision : float sur GPU pour cette session (suffisant pour démo). Le 
  passage float/double sur les vrais kernels FEM sera décidé en session 
  ultérieure.
- Pas de framework graphique (pas de SDL, pas de GLFW, pas d'ImGui) — 
  ON N'AFFICHE RIEN cette session, c'est du compute pur
- Code de test dans tests/test_metal_hello.cpp avec un main dédié

## MÉTHODE DE TRAVAIL ATTENDUE

1. Lis le PHASE_1_TO_PHASE_2_HANDOFF.md complet
2. Lis le CLAUDE.md actuel
3. Présente ton plan d'action AVANT de coder
4. Code par étapes, test à chaque fois
5. À la fin, propose la mise à jour du CLAUDE.md

## DRAPEAUX ROUGES

Tu m'interromps et demandes confirmation si :
- Tu ne trouves pas metal-cpp à l'URL officielle (donne-moi le lien à 
  utiliser, je télécharge à la main si besoin)
- Le linkage Metal/Foundation/QuartzCore ne marche pas (ça arrive parfois 
  avec les versions Xcode récentes)
- Le device Metal ne s'initialise pas (problème de droits / SIP / autre)

Pas de "ça devrait marcher" sans vérification.

## DÉMARRAGE

Commence par : lecture des documents → présentation du plan → début 
d'implémentation. Ne saute pas l'étape de lecture, le HANDOFF contient 
des conventions à respecter.
