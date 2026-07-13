# PHASE 5 — CHT (Conjugate Heat Transfer) : advection-diffusion couplée

## Objectif
Solveur de température CHT : `−∇·(k(γ)∇T) + u·∇T = Q`, où u est un champ de
vitesse donné (de Stokes-Brinkman), k(γ) interpole conduction solide/fluide.
Stabilisation SUPG pour l'advection. CPU double (oracle). Validé sur
advection-diffusion 1D analytique + limite conduction. Pas d'adjoint.

## Contexte TopOptP5
- Réutilise `Grid3D`, Q1 trilinéaire scalaire (1 DOF/nœud pour T).
- Le solveur thermique pur (conduction) existe en P4 (`ThermalSolver`, GPU) et
  l'assemblage Laplacien Eigen dans `ThermoElasticAdjoint` (CPU) — s'en inspirer
  pour le bloc diffusion. Style : FEM3D, 0 warning.
- u = champ de vitesse (Vec, 3·nNodes ou par élément) ; pour l'oracle, u imposé
  analytiquement (pas besoin de résoudre Stokes ici).

## Formulation
`−∇·(k(γ)∇T) + u·∇T = Q` (advection-diffusion stationnaire).
- Diffusion : `K_d = ∫ k(γ) ∇w·∇T` (comme la conduction P4 ; k(γ) évalué au Gauss).
  k(γ) = k_s + (k_f − k_s)·γ (les deux phases conduisent ; fluide advecte en plus).
- Advection : `C_a = ∫ w (u·∇T)` — **non-symétrique** (système non symétrique →
  solveur direct Eigen SparseLU, ou BiCGStab ; direct pour l'oracle).
- **SUPG** : ajouter `∫ τ_supg (u·∇w)(u·∇T − Q)` (résidu advection-diffusion
  projeté sur u·∇w), `τ_supg = h/(2|u|)·(coth(Pe_e) − 1/Pe_e)`, Pe_e = |u|h/(2k).
  (documenter ; sans SUPG, oscillations à haut Péclet — le piège à montrer.)

## Validation — ORACLE 1 : advection-diffusion 1D (quantitatif)
Domaine [0,L] en x (invariance y,z), vitesse uniforme u=(a,0,0), k uniforme, Q=0,
T(0)=0, T(L)=1. Solution : `T(x) = (exp(Pe·x/L) − 1)/(exp(Pe) − 1)`, Pe=aL/k.
- Choisir Pe modéré (~5) : couche limite nette près de x=L.
- **PASS si** max|T_FEM − T_analytique| < 2 % à nx=20, convergence O(h²), et
  **pas d'oscillation** (avec SUPG). Montrer qu'à Pe élevé (~20-50) sans SUPG le
  champ oscille, avec SUPG il est propre (le piège advection).

## Validation — ORACLE 2 : limite conduction (u=0)
u=0 → `−∇·(k∇T)=Q` : profil linéaire T(x)=x/L pour T(0)=0,T(L)=1, Q=0.
- **PASS si** erreur < 1e-6 (reproduit exactement le cas conduction, cohérent P4).

## Livrables
- `src/physics/CHTSolver.{hpp,cpp}` : assemblage diffusion k(γ) + advection u·∇T
  + SUPG, BC Dirichlet, solve direct Eigen (non-symétrique).
- `tests/test_cht.cpp` (CPU pur) : oracle 1 (adv-diff 1D, + démo Péclet élevé
  avec/sans SUPG) + oracle 2 (conduction). Cible Makefile CPU-pure.
- `make` 0 warning. NE PAS committer. Rapporter : erreur adv-diff + Pe testé,
  effet SUPG à haut Pe, erreur limite conduction.

## Garde-fous
- LL-008 (clamp γ, bornes). LL-010 : **clean build** après modif de header.
- Système non-symétrique : vérifier que le solveur direct converge (SparseLU).
