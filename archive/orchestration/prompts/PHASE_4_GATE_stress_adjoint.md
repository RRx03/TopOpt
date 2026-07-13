# PHASE 4 — GATE : sensibilité de la contrainte de stress (p-norm) validée par DF

> Deuxième gate de Phase 4. Tout nouvel objectif différentié doit matcher les
> différences finies < 1e-5 avant d'être utilisé en optimisation (règle actée
> après le gate adjoint compliance).

## Contexte (déjà en place dans TopOptP4)
- `ThermoElasticAdjoint` (src/adjoint) : adjoint 2 blocs validé pour J = Lᵀ U
  (compliance), CPU double Eigen. forward = thermo k(ρ)→T → F_th(T) → élasto.
- `StressModel` (src/topopt/StressModel.hpp) : von Mises solide `vm0_e`,
  stress relaxé `σ_e = ρ_e^q vm0_e`, p-norm `σ_PN = (Σ σ_e^P)^{1/P}`.
- `H8Element::stressMatrix` (S0 = D0·B centroïde), `vonMisesForm` (V).

## Objectif de cette session
Calculer `dσ_PN/dρ` par l'adjoint 2 blocs et le **valider par DF centrées
< 1e-5** sur 8³. Réutiliser au maximum `ThermoElasticAdjoint` (mêmes 2 solves
adjoints, mêmes ∂K/∂ρ ; seuls changent le RHS élastique et le terme explicite).

## Dérivation (à implémenter)
J = σ_PN = (Σ_e σ_e^P)^{1/P}, σ_e = ρ_e^q vm0_e, vm0_e = √(s_eᵀ V s_e), s_e = S0 u_e.

- `∂J/∂σ_e = σ_PN^{1−P} · σ_e^{P−1}`.
- **Terme explicite** `∂J/∂ρ_i|exp = ∂J/∂σ_i · q ρ_i^{q−1} vm0_i`  (LL-008 : clamp ρ).
- **Sensibilité au déplacement** : `∂σ_e/∂u_e = ρ_e^q (1/vm0_e) S0ᵀ V s_e`  (24×1) ;
  `∂J/∂U = Σ_e (∂J/∂σ_e)(∂σ_e/∂u_e)` scatterisé aux DOF globaux. (gérer vm0_e≈0.)
- **Adjoints** (one-way, comme le gate compliance) :
  `K_e λ_e = −(∂J/∂U)`,  `K_t λ_t = Gᵀ λ_e`  (G = ∂F_th/∂T).
- **Gradient** :
  `dJ/dρ_i = ∂J/∂ρ_i|exp + λ_eᵀ[(∂K_e/∂ρ_i)U − ∂F_th/∂ρ_i] + λ_tᵀ(∂K_t/∂ρ_i)T`
  (les 3 derniers termes IDENTIQUES à ceux de ThermoElasticAdjoint).

## Validation (oracle)
- 8³, ρ aléatoire [0.3,0.7] (graine fixe), P=8, q=0.5.
- DF centrées ε=1e-6, 20 éléments aléatoires, forward complet à chaque éval
  (thermo+élasto en Eigen double, puis σ_PN via StressModel).
- PASS : max |adjoint−DF|/|DF| < 1e-5.

## Livrables
- Étendre `ThermoElasticAdjoint` (méthode `stressPNorm(rho)` + `stressPNormGrad(rho)`)
  ou nouvelle classe `StressAdjoint` réutilisant les solves.
- `tests/test_stress_adjoint_fd.cpp` (CPU pur), ajouté au Makefile + `make test`/`test_cpu`.
- `make` 0 warning. NE PAS committer (le parent vérifie et committe).
- Rapporter : erreur relative max, et confirmer que les 3 termes hérités + le
  terme explicite + le terme via ∂J/∂U sont tous actifs.
