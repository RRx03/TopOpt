# PHASE 5R — Contrainte T_max : adjoint thermique-objectif, gate DF

## Objectif
Objectif/contrainte de **température de paroi maximale** J_T (p-norm de T sur le
solide) et son gradient dJ_T/dγ par adjoint (back-half thermique+Stokes), validé
par DF (< 1e-3). Permet la vraie contrainte cooling jacket « T_paroi ≤ T_max ».
CPU double. Réutilise la machinerie de `TripleAdjoint`.

## Contexte
- `TripleAdjoint` a déjà : forward Stokes-Brinkman → CHT → (élasto), et les solves
  adjoints K_tᵀ, Aᵀ, le couplage (∂R_t/∂u)ᵀ. Réutilise/étends ces pièces.
- CHT : K_t(γ,u) T = f_t (diffusion k(γ) + advection u·∇T, SANS SUPG, Pe modéré).

## Formulation
J_T = ( Σ_e s_e · T_e^P )^{1/P}, T_e = température moyenne élément, s_e = poids
« solidité » = (1−γ_e) (on mesure la température DANS le solide/paroi). P=8.
Dépend de : γ (via k(γ), via s_e=1−γ), u (advection), T.
- **Adjoint** (PAS de bloc élastique) :
  1. K_tᵀ λ_t = −∂J_T/∂T. ∂J_T/∂T_e = J_T^{1−P} s_e T_e^{P−1} (réparti aux nœuds
     de l'élément selon la moyenne T_e = moyenne nodale).
  2. Aᵀ λ_s = −(∂R_t/∂u)ᵀ λ_t (même couplage thermique→Stokes que TripleAdjoint).
- **Gradient** :
  dJ_T/dγ_i = ∂J_T/∂γ_i|exp + λ_tᵀ(∂K_t/∂γ_i)T + λ_sᵀ(∂A/∂γ_i)w
  - ∂J_T/∂γ_i|exp = ∂J_T/∂s_i · (ds_i/dγ_i=−1) = −J_T^{1−P} T_i^P / P·... (dérive
    proprement le terme explicite via s_e=1−γ ; clamp, LL-008).
  - ∂K_t/∂γ_i = dk_i L0_i ; ∂A/∂γ_i = dα_i M_vel_i.

## Validation (oracle)
- 6³ ou 8³, γ aléatoire [0.3,0.7] graine fixe, Pe modéré, α_max~1e2. 15-20 éléments.
- DF centrées ε=1e-6 (4ᵉ ordre si plancher, LL-009). **PASS si < 1e-3** (accord absolu).
- Imprimer Σ|terme_explicite|, Σ|terme_thermal|, Σ|terme_stokes| (3 non triviaux).

## Livrables
- `src/adjoint/ThermalObjectiveAdjoint.{hpp,cpp}` (ou méthode dans TripleAdjoint).
- `tests/test_tmax_adjoint_fd.cpp` (CPU pur), cible Makefile CPU-pure.
- `make` 0 warning, clean build (LL-010). NE PAS committer. Rapporter erreur max,
  accord absolu, Σ des 3 termes.

## Garde-fous
LL-008/009/010, oracle CPU double. Non-régression des tests existants.
