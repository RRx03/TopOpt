# PHASE 5 — Brinkman penalization (frontière fluide-solide = design)

## Objectif
Étendre le solveur Stokes (`StokesSolver`, Q1-Q1 PSPG, validé) avec la
**Brinkman penalization** `α(γ)u` dans l'équation de moment, et le valider
quantitativement (Darcy-Brinkman analytique + non-fuite). CPU double (oracle).
Pas d'adjoint, pas de CHT — juste le primal Stokes-Brinkman.

## Formulation
Moment : −μ∇²u + ∇p + **α(γ)u** = f, ∇·u = 0.
- Champ de design γ par élément : **γ=1 → fluide** (α≈0), **γ=0 → solide** (α=α_max).
- Interpolation (Borrvall-Petersson, convexe) :
  `α(γ) = α_max + (α_min − α_max)·γ·(1+q)/(γ+q)`, q ~ 0.1, α_min ≈ 0.
  (documenter q, α_min, α_max ; α_max ∈ [1e3, 1e5] selon μ, LL-LIT-004).
- Assemblage : ajouter au bloc A le terme `∫ α(γ) u·v` (masse pondérée par α,
  par élément, α évalué au point de Gauss depuis γ_e). Le reste (visqueux,
  couplage B/Bᵀ, PSPG) inchangé.

## Validation — ORACLE 1 : Darcy-Brinkman 1D (quantitatif)
Canal plan, α UNIFORME (γ uniforme), force volumique G, no-slip x=0,x=L.
Solution analytique : `u(x) = (G/α)[1 − cosh(κ(x−L/2)) / cosh(κL/2)]`,
κ = √(α/μ). Choisir α tel que κL ~ 2–6 (profil nettement différent de la parabole).
- **PASS si** max|u_FEM − u_analytique|/u_max < 2 % à nx=20, et convergence O(h²).
- Vérifier les limites : α→0 redonne la parabole de Poiseuille ; α très grand → u→G/α→0.

## Validation — ORACLE 2 : non-fuite (LL-LIT-004)
Canal avec un **bloc solide** (γ=0, donc α=α_max) obstruant une partie de la
section ; le reste fluide (γ=1). Écoulement imposé (pression ou force).
- **PASS si** la vitesse moyenne dans le bloc solide / vitesse débitante fluide
  < 1 % (pas de fuite) pour α_max suffisant. Montrer que baisser α_max (ex. 1e1)
  fait remonter la fuite — calibration.

## Livrables
- Étendre `StokesSolver` (ou `StokesBrinkman`) : champ γ par élément, interpolation
  α(γ), terme de masse pondéré ; garder le mode α=0 (Stokes pur) fonctionnel.
- `tests/test_brinkman.cpp` (CPU pur) : oracle 1 (Darcy-Brinkman) + oracle 2
  (non-fuite) + limite α→0. Cible Makefile CPU-pure (all/test/test_cpu/clean).
- `make` 0 warning. NE PAS committer. Rapporter : erreur Darcy-Brinkman + κL testé,
  taux de fuite vs α_max, confirmation limite α→0.

## Garde-fous
- LL-LIT-004 : α_max trop faible = fuite ; trop grand = conditionnement explose
  (surveiller la solvabilité du système direct). Documenter le sweet spot trouvé.
- LL-008 : clamp γ avant interpolation ; bornes.
