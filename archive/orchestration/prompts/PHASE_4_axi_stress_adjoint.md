# PHASE 4 — Étape 7a : adjoint stress axisymétrique (élastique seul) validé DF

## Objectif
Sensibilité dσ_PN/dρ de la contrainte de stress p-norm sur la piste
axisymétrique (élastique seul, PAS de thermique), validée par DF < 1e-5.
Prérequis de l'optimisation tuyère axisym (7b).

## Contexte TopOptP4 (piste axisym déjà en place)
- `Grid2DAxi`, `AxiQ4Element` (stiffness(r,z,nu) 8×8, stressMatrix au centroïde 4×8),
  `FEM2DAxi` (assemblage + LDLT + pression interne + elementStress).
- Modèle de relaxation/p-norm : réutilise le concept de `StressModel` (von Mises
  relaxé ρ^q, p-norm), mais en **axisym 4 composantes** [σr,σz,σθ,τrz].
- Adjoint 3D déjà validé (`ThermoElasticAdjoint`) — même structure, ici plus simple
  (élastique seul, pas de bloc thermique).

## Formulation
- von Mises axisym : vm0_e=√(s_eᵀ Vax s_e), s_e=S0ax·u_e (4×1), avec
  Vax=[[1,−.5,−.5,0],[−.5,1,−.5,0],[−.5,−.5,1,0],[0,0,0,3]].
- stress relaxé σ_e=ρ_e^q vm0_e ; p-norm σ_PN=(Σσ_e^P)^{1/P}.
- Forward : K_e(ρ)U=F (F = pression interne fixe). E_e=Emin+ρ^p(E0−Emin).
- Adjoint (élastique seul) : K_e λ = −∂σ_PN/∂U.
  ∂σ_PN/∂σ_e=σ_PN^{1−P}σ_e^{P−1} ; ∂σ_e/∂u_e=ρ^q (1/vm0_e) S0axᵀ Vax s_e ;
  ∂σ_PN/∂U = Σ_e (∂σ_PN/∂σ_e)(∂σ_e/∂u_e) scatterisé.
- Gradient : dσ_PN/dρ_i = ∂σ_PN/∂ρ_i|exp + λᵢᵀ (dE_i KE0ax_i) u_i,
  avec ∂σ_PN/∂ρ_i|exp = (∂σ_PN/∂σ_i) q ρ_i^{q−1} vm0_i (clamp/floor ρ, LL-008),
  dE_i = p ρ_i^{p−1}(E0−Emin), **KE0ax_i = stiffness de l'élément i à E=1**
  (ATTENTION : en axisym la matrice élémentaire dépend de r, donc PROPRE à chaque
  élément — pas de KE0 partagée comme en 3D).

## Validation (oracle)
- Grille axisym ~10×10 (a=1,b=2,H=2), ρ aléatoire [0.3,0.7] graine fixe, P=8, q=0.5.
- BC : pression interne sur r=a, u_z=0 sur z=0/z=H (plane strain).
- DF centrées (2ᵉ ordre ε=1e-6 ; si plancher d'arrondi sur gradients quasi-nuls,
  passer en 4ᵉ ordre — cf. LL-009), 20 éléments aléatoires.
- PASS : max |adjoint−DF|/|DF| < 1e-5 (juger aussi sur l'accord absolu, LL-009).

## Livrables
- Étendre la piste axisym (ex. `src/adjoint/AxiStressAdjoint.{hpp,cpp}` ou méthodes
  dans FEM2DAxi) + `src/topopt/StressModelAxi.hpp` (von Mises 4-comp + p-norm).
- `tests/test_axi_stress_adjoint_fd.cpp` (CPU pur), cible Makefile + all/test/test_cpu/clean.
- `make` 0 warning. NE PAS committer. Rapporter erreur max + accord absolu + que le
  terme explicite ET le terme via ∂J/∂U sont actifs.
