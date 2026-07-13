# PHASE 4 — GATE BLOQUANT : adjoint thermo-élastique 2 blocs validé par DF

> **Gate de la Phase 4 (LL-LIT-007).** Tant que ce gate n'est pas vert
> (matching adjoint vs différences finies < 1e-5 sur petit cas), **interdiction
> d'avancer** vers le stress/MMA/tuyère. C'est le cœur scientifique du projet.

## INSTRUCTIONS POUR L'UTILISATEUR
Copier le bloc « PROMPT » dans une session Claude Code fraîche lancée dans
`TopOptP4/` (ou laisser Claude principal lancer un sous-agent avec ce spec).

---

# PROMPT À DONNER À CLAUDE CODE

Tu travailles dans `TopOptP4/`. État : Phase 4 étapes 1-2 faites — solveur
thermique (`ThermalSolver`), couplage thermo-élastique faible
(`ThermoElasticCoupling`, `H8Element::thermalCoupling`), solveur élastique
matrix-free (`CGSolver3D`), SIMP (`SIMP3D`). Lis d'abord `CLAUDE.md`,
`../orchestration/MASTER_CLAUDE.md`, `../orchestration/LESSONS_LEARNED.md`
(surtout LL-006, LL-007, LL-008), et `PHASE_2_REPORT.md`/`PHASE_3_REPORT.md`.

## Objectif unique de cette session
Implémenter l'**adjoint discret 2 blocs** (méca + thermo, couplage one-way) pour
la sensibilité d'un objectif par rapport à la densité ρ, et le **valider par
différences finies centrées** sur un cas 10×10×10, tolérance **< 1e-5**.
**Ne rien coder d'autre** (pas de von Mises, pas de MMA, pas de tuyère).

## Problème discret (one-way thermo-élastique)
- Thermique : `K_t(ρ) T = Q`  (Q source fixe, indépendante de ρ).
- Élasticité : `K_e(ρ) U = F_mech + F_th(ρ, T)`,
  avec `F_th = Σ_e E_e(ρ) α C_e (T_e − T_ref)` (C_e = `H8Element::thermalCoupling`).
  → F_th dépend de ρ (via E_e = SIMP) ET de T.
- Interpolations SIMP : `E_e = Emin + ρ_e^p (E0−Emin)` (élastique) ;
  conductivité `k_e = kmin + ρ_e^q (k0−kmin)` (thermique). Documenter p, q.
- Objectif de validation : **J = Lᵀ U** (fonctionnelle linéaire, L vecteur fixe ;
  prendre L = F_mech, ou un probe fixe). Choix simple à dérivée propre.

## Dérivation adjointe attendue (à implémenter, pas à redécouvrir)
Lagrangien `L = J + λ_eᵀ(K_e U − F_mech − F_th) + λ_tᵀ(K_t T − Q)`.
1. **Adjoint élastique** : `K_e λ_e = −L`  (K_e SPD symétrique).
2. **Adjoint thermique** : `K_t λ_t = Gᵀ λ_e`, où `G = ∂F_th/∂T`
   (matrice nDof×nNodes ; par élément `∂f_th_e/∂T_e = E_e α C_e`). Le terme
   `Gᵀλ_e` est le **couplage** : l'adjoint thermique est piloté par l'adjoint
   élastique (signature du one-way).
3. **Gradient** :
   `dJ/dρ_i = λ_eᵀ[ (∂K_e/∂ρ_i) U − ∂F_th/∂ρ_i ] + λ_tᵀ (∂K_t/∂ρ_i) T`
   avec `∂F_th/∂ρ_i = (∂E_i/∂ρ_i) α C_i (T_i − T_ref)` (élément i),
   `∂K_e/∂ρ_i = (∂E_i/∂ρ_i) KE0_i`, `∂K_t/∂ρ_i = (∂k_i/∂ρ_i) L0_i`.
   Termes locaux à l'élément i → assemblage direct, O(nElems).

## Validation (l'ORACLE du gate)
- Cas 10×10×10 (ou 8³), ρ aléatoire dans [0.3, 0.7] (éviter les bords 0/1).
- DF centrées : `dJ/dρ_i ≈ (J(ρ+ε e_i) − J(ρ−ε e_i)) / (2ε)`, ε = 1e-6,
  chaque J nécessitant un forward complet (thermo puis élasto).
- Spot-check : **20 éléments tirés au hasard** (pas tout le vecteur, coûteux).
- **Critère PASS : max |adjoint_i − DF_i| / |DF_i| < 1e-5** sur les 20.
- Solveurs : pour la validation, **utiliser des solves serrés** (CG tol 1e-10 ou
  un solveur direct CPU Eigen sur le petit cas) — sinon le bruit du CG float32
  masque l'erreur d'adjoint. Recommandé : **chemin de validation CPU double
  précision** (réutiliser `FEM3D` pour l'élastique, assembler K_t en Eigen pour
  le thermique) afin que l'oracle soit propre. Le GPU reste pour la production.
- Pièges : signes (cf. dérivation), one-way (NE PAS ajouter de terme ∂R_t/∂U),
  `∂F_th/∂ρ` souvent oublié → fait échouer la DF. LL-008 : clamp ρ avant pow.

## Livrables
- `src/adjoint/ThermoElasticAdjoint.{hpp,cpp}` (ou équivalent).
- `tests/test_adjoint_fd.cpp` : imprime, pour 20 éléments, adjoint vs DF + erreur
  relative max, et PASS/FAIL (<1e-5). Ajouté au Makefile + `make test`.
- `make` 0 warning. Commit proposé (non exécuté sans accord) :
  `feat(phase4): two-block thermo-elastic adjoint validated by FD (<1e-5)`.

## Critère de fin de session (GATE)
`./build/test_adjoint_fd` imprime PASS avec erreur relative max < 1e-5.
Si tu n'y arrives pas, **n'invente pas** : documente l'écart, l'erreur relative
obtenue, et les hypothèses testées. Ne maquille jamais un échec de gate.
