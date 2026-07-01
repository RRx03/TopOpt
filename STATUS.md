# Status — 2026-06-29

## Current focus
**Phase 5 (TRÈS CRITIQUE)** : Stokes + CHT, adjoint triple-couplé. Session 1 faite.

## Décisions actées
- ADR-017 : éléments **Q1-Q1 + PSPG** (pas Taylor-Hood) — cohérence grille structurée.
- ADR-018 : oracles en CPU double (Eigen direct) ; GPU = production différée.

## Progression Phase 5
1. ✅ **Solveur Stokes** Q1-Q1 PSPG (`StokesSolver`) — validé Poiseuille :
   - force volumique (valide A, p≡0) : u_z exact, O(h²).
   - **piloté par pression** (valide couplage B/Bᵀ, pression linéaire non triviale) :
     u_z O(h²) 7.4e-3→1.8e-3, linéarité pression 3.3%→1.5%, pas de damier.
   - inf-sup confirmé (α=1e-7 → pression 4e4× plus bruitée → PSPG décisif).
3. ✅ **Brinkman penalization** α(γ)u (`StokesSolver::setBrinkman`) — validé :
   Darcy-Brinkman 1D analytique (cosh) relErr 1.2e-3 O(h²) ; α→0 = Poiseuille ;
   non-fuite calibrée, sweet spot α_max=1e4 (fuite 0.47%). LL-LIT-004 respecté.

## Next up (brief Phase 5)
2. (différé GPU) Solveur saddle-point itératif MINRES/Uzawa — production ; le direct
   Eigen suffit pour les oracles CPU. À porter avec le GPU.
4. **CHT** : advection-diffusion couplée, validation analytique.
5. **Adjoint 3 blocs validé DF (1e-3)** — gate le plus dur du projet.
6. Heaviside + intégration MMA. 7. Cooling jacket tuyère. 8. Clôture.

## Note
- Couche limite pression O(h) du PSPG égal-ordre près des bords ∇p≠0 : propriété
  connue (pas un bug), pression validée par convergence.

## Don't touch
- `../TopOptP1..P4` (figés). `third_party/*` (symlinks).
