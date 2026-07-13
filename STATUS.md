# Status — 2026-06-29

## Current focus
**Phase 5 CLÔTURÉE** (TRÈS CRITIQUE) : TO multiphysique fluide-structure-thermique.

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
4. ✅ **CHT** (`CHTSolver`) — advection-diffusion + SUPG : conduction exacte 4.4e-15,
   adv-diff 1D O(h²), piège Péclet démontré (Galerkin oscille, SUPG propre).
   → **PRIMAL TRIPLE COMPLET** : Stokes-Brinkman→u, CHT→T, thermo-élastique→U.
5. ✅ **ADJOINT TRIPLE-COUPLÉ validé par DF** (`TripleAdjoint`) — **GATE le plus dur
   du projet FRANCHI** : max rel 2.1e-7 / abs 7e-9 (tol 1e-3), 3 termes actifs.
   Le cœur scientifique (TO multiphysique fluide-structure-thermique par adjoint)
   est validé de bout en bout.

## Phase 5 — complète
6. ✅ **Démonstrateur TO multiphysique** (`cooling_jacket`) : MMA + filtre 3D +
   Heaviside + gradient TripleAdjoint validé. J −71% convergé, volume actif,
   quasi-binaire (gris 0.056), **ratio fluide col/extrémités 2.16×**. VTK ParaView.
   → **Cœur scientifique validé de bout en bout.**

## Next : Phase 6 (à arbitrer) + différés
→ `../orchestration/handoffs/PHASE_5_TO_6.md`, `../orchestration/prompts/PHASE_6_BRIEF.md`.
Recommandation : solder d'abord les différés « recruteur » (contraintes cooling jacket
réelles + vraie tuyère + comparaison Borrvall-Petersson) avant de choisir A/B/C.

Différés (report §7) : contraintes spécifiques (T_max/ΔP/von Mises, chacune un adjoint
DF), vraie géométrie tuyère, adjoint SUPG, portage GPU.

## Note
- Couche limite pression O(h) du PSPG égal-ordre près des bords ∇p≠0 : propriété
  connue (pas un bug), pression validée par convergence.

## Don't touch
- `../TopOptP1..P4` (figés). `third_party/*` (symlinks).
