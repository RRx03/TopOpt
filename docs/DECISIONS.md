# Decisions — TopOptP2 (Phase 2)

## ADR-001 : TopOptP2 démarre vierge (Metal-only), non copié de Phase 1
- **Date** : 2026-06-15
- **Contexte** : Phase 2 vit dans un dossier frère (séquençage/traçabilité).
  Faut-il y copier le code 2D de Phase 1 pour l'étendre ?
- **Options considérées** : copie complète de P1 ; vierge + Metal seul ;
  copie src + re-vendoring des deps.
- **Décision** : vierge + Metal seul. Le 2D sera porté en session ultérieure.
- **Conséquences** : fondation Metal isolée et lisible ; le portage 3D du 2D
  reste à planifier (cf. TASKS.md).

## ADR-002 : Réutiliser le metal-cpp officiel déjà présent localement
- **Date** : 2026-06-15
- **Contexte** : vendoring metal-cpp ; une copie officielle Apple (macOS26/
  iOS26) existe déjà dans `/Users/romanroux/Dev/Metal/metal-cpp`.
- **Options considérées** : télécharger depuis developer.apple.com ;
  réutiliser la copie locale.
- **Décision** : copier la copie locale (Foundation/Metal/QuartzCore + LICENSE).
  Version alignée avec le SDK local (26.2), pas de dépendance réseau.
- **Conséquences** : vendoring déterministe et hors-ligne ; revérifier la
  version si le SDK change.

## ADR-003 : Build zéro warning — headers vendorisés en -isystem, -fno-objc-arc
- **Date** : 2026-06-15
- **Contexte** : garder `-Wall -Wextra -Wpedantic` à zéro warning tout en
  utilisant metal-cpp (manual ref counting, headers volumineux).
- **Décision** : metal-cpp en `-isystem` (silence les warnings tiers) ;
  `-fno-objc-arc` sur les TU Metal ; `*_PRIVATE_IMPLEMENTATION` regroupés
  dans un unique `src/gpu/metal_impl.cpp`.
- **Conséquences** : build propre ; ne jamais dupliquer les macros
  d'implémentation (sinon symboles dupliqués au link).

## ADR-004 : Précision float sur GPU pour cette phase (différé pour FEM)
- **Date** : 2026-06-15
- **Contexte** : float vs double sur GPU impacte précision/perf/mémoire.
- **Décision** : float pour la démo de cette session. Le choix float/double
  pour les vrais kernels FEM est explicitement différé à une session ultérieure.
- **Conséquences** : hello-world en float (suffisant) ; décision FEM à acter
  avant l'assembly 3D (test patch 1e-6 en float, 1e-10 en double — cf.
  ../TopOptP1/TRANSITIONS.md).
