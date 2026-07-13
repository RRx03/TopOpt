# PHASE 5 — GATE BLOQUANT ABSOLU : adjoint triple-couplé validé par DF

> **Le gate le plus dur du projet** (LL-LIT-007). Tant qu'il n'est pas vert
> (adjoint vs DF < 1e-3), **interdiction absolue** d'avancer vers l'optimisation
> du cooling jacket. « Si ça ne matche pas, STOP, débugger, ne pas avancer. »

## Problème couplé (cascade one-way γ → (u,p) → T → U)
Design γ par élément, γ=1→fluide, γ=0→solide.
1. **Stokes-Brinkman** : `A(γ) w = f_s`, w=(u,p), A dépend de γ via α(γ) (Brinkman).
2. **CHT** (Galerkin, **SANS SUPG**, Péclet modéré Pe~1-3 pour stabilité propre) :
   `K_t(γ,u) T = f_t`, K_t = diffusion k(γ) + advection ∫N_a(u·∇T). Dépend de γ (k)
   et de u (advection).
3. **Thermo-élastique** : `K_e(γ) U = f_mech + F_th(γ,T)`, E(γ)=Emin+(E0−Emin)(1−γ)^p
   (solide rigide où γ=0), F_th = couplage thermique (comme P4, `H8Element::thermalCoupling`).
Objectif de validation : **J = Lᵀ U** (fonctionnelle linéaire, L fixe).

## Dérivation adjointe (à implémenter — cascade inverse)
Lagrangien `L = J + λ_eᵀR_e + λ_tᵀR_t + λ_sᵀR_s`.
1. **Adjoint élastique** : `K_eᵀ λ_e = −L` (K_e SPD sym → λ_e = −K_e⁻¹L). Comme P4.
2. **Adjoint thermique** : `K_tᵀ λ_t = −(∂R_e/∂T)ᵀ λ_e = Gᵀ λ_e`,
   G = ∂F_th/∂T (même couplage qu'en P4). **K_tᵀ = transpose de K_t** : la diffusion
   est symétrique, mais l'**advection se transpose** (`∫N_a(u·∇N_b)` → `∫N_b(u·∇N_a)`,
   = advection par −u après intégration par parties). Résous K_tᵀ λ_t = Gᵀλ_e (SparseLU
   sur la transposée).
3. **Adjoint Stokes** : `Aᵀ λ_s = −(∂R_t/∂u)ᵀ λ_t`. Le couplage : R_t contient
   l'advection `∫N_a(u·∇T)` → `∂R_t/∂u` (dérivée par rapport au champ vitesse) est
   `∂(C_a(u)T)/∂u`. Pour le DOF vitesse (nœud b, composante c) :
   `∂R_t,a/∂u_{b,c} = ∫ N_a N_b (∂T/∂x_c)`. Donc `(∂R_t/∂u)ᵀ λ_t` est un vecteur sur
   les DOF vitesse ; les DOF pression de λ_s ont un RHS nul. Résous Aᵀ λ_s = ce RHS.
4. **Gradient** :
   `dJ/dγ_i = λ_eᵀ[(∂K_e/∂γ_i)U − ∂F_th/∂γ_i] + λ_tᵀ(∂K_t/∂γ_i)T + λ_sᵀ(∂A/∂γ_i)w`
   - `∂K_e/∂γ_i = dE_i·KE0`, `dE_i = −p(E0−Emin)(1−γ_i)^{p−1}` (E décroît avec γ).
   - `∂F_th/∂γ_i = dE_i·α_th·C·(T_i−Tref)` (le terme −∂F_th/∂γ, ne pas l'oublier).
   - `∂K_t/∂γ_i = dk_i·L0_i` (diffusion ; dk_i=k_f−k_s ; l'advection ne dépend pas de γ).
   - `∂A/∂γ_i = dα_i·M_vel_i` (Brinkman ; dα_i = dα/dγ de Borrvall-Petersson).
   (∂J/∂γ = 0 car J=LᵀU.)

## Validation (oracle)
- Petit cas 6³ ou 8³, γ aléatoire dans [0.3,0.7] (graine fixe), Pe modéré (sans SUPG),
  α_max modéré (ex. 1e2-1e3 pour ne pas trop mal conditionner), P/q docs.
- DF centrées (2ᵉ ordre ε=1e-6 ; passer 4ᵉ ordre si plancher d'arrondi, LL-009),
  **15-20 éléments** tirés au hasard, chaque éval = forward complet (Stokes→CHT→élasto)
  puis J=LᵀU.
- **PASS si max |adjoint−DF|/|DF| < 1e-3** (tolérance du brief ; juger aussi sur
  l'accord absolu, LL-009).
- **Confirmer que les 3 blocs contribuent** : imprimer Σ|terme_stokes|, Σ|terme_thermal|,
  Σ|terme_elastic| — les trois doivent être non triviaux (sinon un couplage est inerte
  et le triple n'est pas réellement testé).

## Livrables
- `src/adjoint/TripleAdjoint.{hpp,cpp}` : forward cascade + 3 adjoints + gradient.
  Réutiliser StokesSolver (A, Aᵀ), CHTSolver (K_t, K_tᵀ, ∂R_t/∂u), et la machinerie
  élastique+couplage de ThermoElasticAdjoint (K_e, G, F_th).
- `tests/test_triple_adjoint_fd.cpp` (CPU pur), cible Makefile + all/test/test_cpu/clean.
- `make` 0 warning. **Clean build** (LL-010). NE PAS committer.
- Rapporter : erreur max, accord absolu, Σ des 3 termes (preuve triple actif), ε retenu.

## Garde-fous
- Le terme le plus piégeux : `∂R_t/∂u` (couplage thermique→Stokes) et la transpose
  de l'advection. Si la DF ne matche pas, suspecter d'abord CE terme et les signes.
- SUPG exclu du gate (Péclet modéré) : différentiation propre. L'adjoint SUPG =
  raffinement production ultérieur, à re-valider alors.
- LL-008 (clamp γ), LL-009 (juger accord absolu), oracle CPU double (ADR-018).
