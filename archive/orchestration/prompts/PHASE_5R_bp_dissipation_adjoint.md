# PHASE 5R (différés recruteur) — Borrvall-Petersson : adjoint de dissipation, gate DF

## Objectif
Objectif de **puissance dissipée** de Stokes `Φ(γ,u) = ½∫(μ|∇u|² + α(γ)|u|²)` et son
gradient `dΦ/dγ` par adjoint, validé par DF (< 1e-4). Prérequis de la reproduction
du cas Borrvall-Petersson (TO fluide canonique publié). CPU double.

## Contexte TopOptP5
- `StokesSolver` (Q1-Q1 PSPG + Brinkman α(γ), validé). N'expose que solve() → il
  faudra assembler A(γ) explicitement (comme `TripleAdjoint` l'a fait) pour l'adjoint.
- `TripleAdjoint` : exemple d'assemblage explicite Stokes A + Aᵀ + gradient. S'en inspirer.
- Interpolation Brinkman Borrvall-Petersson : α(γ)=α_max+(α_min−α_max)γ(1+q)/(γ+q),
  γ=1 fluide, γ=0 solide.

## Formulation
Φ(γ,u) = ½∫ μ ∇u:∇u + ½∫ α(γ) u·u  (puissance dissipée, u = solution Stokes-Brinkman).
- **Terme explicite** : ∂Φ/∂γ_i = ½ (dα/dγ_i) ∫_e |u|²  (via α(γ)).
- **Terme implicite** via u : ∂Φ/∂u = ∫(μ∇u:∇v + α u·v) = (bloc vitesse de A)·u.
  Adjoint : Aᵀ λ = −∂Φ/∂w (w=(u,p) ; ∂Φ/∂p = 0). Note : le problème de dissipation
  Stokes est **quasi self-adjoint** (∂Φ/∂u ≈ A_uu u) — attends-toi à λ_u ≈ −u ; c'est
  un bon check de cohérence, mais **valide par DF de toute façon**.
- Gradient : dΦ/dγ_i = ∂Φ/∂γ_i + λᵀ (∂A/∂γ_i) w, ∂A/∂γ_i = dα_i M_vel_i (Brinkman).

## Validation (oracle)
- Petit cas quasi-2D (slab mince en z, slip sur faces z → écoulement 2D), ex. 12×12×1
  ou 16×16×1. γ aléatoire [0.3,0.7] graine fixe. BC : entrée vitesse imposée, sortie
  libre, no-slip parois. α_max modéré (1e2-1e3).
- DF centrées ε=1e-6 (4ᵉ ordre si plancher, LL-009), 15-20 éléments.
- **PASS si max |adjoint−DF|/|DF| < 1e-4** (juger aussi accord absolu, LL-009).
- Imprimer Σ|terme_explicite|, Σ|terme_adjoint| — les deux non triviaux.

## Livrables
- `src/adjoint/DissipationAdjoint.{hpp,cpp}` (ou méthode dans un module fluide) :
  forward Stokes-Brinkman + Φ + adjoint + gradient. Assemblage explicite A/Aᵀ.
- `tests/test_dissipation_adjoint_fd.cpp` (CPU pur), cible Makefile CPU-pure.
- `make` 0 warning, clean build (LL-010). NE PAS committer. Rapporter erreur max,
  accord absolu, Σ des 2 termes, et si λ_u ≈ −u (check self-adjoint).

## Garde-fous
LL-008 (clamp γ), LL-009, LL-010, oracle CPU double.
