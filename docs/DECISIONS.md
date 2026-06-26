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

## ADR-005 : Solveur élastique matrix-free (pas de K assemblée)
- **Date** : 2026-06-26
- **Contexte** : produit K·u au cœur du CG. CSR assemblée à 128³ ≈ 2-4 GB et exige
  un assembly GPU avec atomicAdd (races).
- **Options** : CSR assemblé + SpMV ; matrix-free node-gather ; CSR puis matrix-free.
- **Décision** : **matrix-free**. K·u recalculé par nœud depuis KE0 (constant) × Emod.
- **Conséquences** : ~150 MB au lieu de 2-4 GB à 128³, pas d'atomics, scalable.
  Diverge du plan CSR initial (validé par l'utilisateur). Prolongation/restriction
  Phase 3 opèrent sans structure CSR.

## ADR-006 : Emin = 1e-4 pour le solveur itératif (vs 1e-9 direct en P1)
- **Date** : 2026-06-26
- **Contexte** : SIMP Emin=1e-9 (P1, solveur direct) rend K quasi-singulière dans le
  vide → CG Jacobi float32 diverge (cond ~1e9).
- **Décision** : Emin = 1e-4·E0 pour la voie itérative GPU.
- **Conséquences** : CG converge, compliance monotone. Phase 3 (multigrid) pourra
  rebaisser Emin. Cf. LL-006.

## ADR-007 : Filtre Helmholtz GPU matrix-free ; OC exploite la conservation de volume
- **Date** : 2026-06-26
- **Contexte** : filtre PDE appliqué ~15× par itération si re-filtré dans la
  bissection OC (coûteux à 128³).
- **Décision** : filtre matrix-free scalaire GPU ; la bissection OC se fait sur
  `rho.sum()` (le filtre conserve la moyenne), un seul filtrage par itération.
- **Conséquences** : 60×20×20 : 73 s → 51.7 s, volume tenu. Cf. LL-007.

## ADR-008 : Visualisation = surface des voxels solides en STL binaire
- **Date** : 2026-06-26
- **Contexte** : besoin d'un STL ouvrable (MeshLab) du design.
- **Décision** : émettre les faces de bord des cellules ρ≥0.5 (watertight).
- **Conséquences** : robuste et manifold ; iso-surface lisse (marching cubes)
  différée en Phase 3.
