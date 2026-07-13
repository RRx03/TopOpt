# PHASE 5 — Session 1 : solveur Stokes Q1-Q1 PSPG, validé Poiseuille

## Objectif
Solveur de Stokes incompressible 3D, éléments **Q1-Q1 stabilisés PSPG**
(ADR-017), CPU double précision (Eigen direct, oracle), validé contre la solution
analytique de Poiseuille (< 1 %), **sans oscillation de pression**. Pas de
Brinkman, pas de couplage, pas d'adjoint — uniquement le solveur primal Stokes.

## Contexte TopOptP5
- Réutilise `Grid3D` (grille structurée H8). Q1 trilinéaire pour u ET p.
- 4 DOF/nœud : (u_x, u_y, u_z, p). Conventions, style, 0 warning : cf. FEM3D.
- Oracle CPU double, comme tous les gates des phases précédentes (ADR-018).

## Formulation (Q1-Q1 + stabilisation pression)
Stokes : −μ∇²u + ∇p = f, ∇·u = 0. Forme faible Galerkin + stabilisation
(Q1-Q1 viole inf-sup → indispensable, sinon mode pression parasite) :
- Visqueux : A = ∫ μ ∇u : ∇v
- Couplage : B^T = −∫ p (∇·v) ; B = −∫ q (∇·u)
- **Stabilisation Brezzi-Pitkäranta / PSPG** : C = ∫ τ ∇q·∇p, τ = α_stab h²/μ
  (α_stab ~ 1/12 à 1/3, documenter). (Optionnel : PSPG complet avec résidu de
  moment ; pour Stokes linéaire la version pression-Laplacien suffit et est propre.)
- Système saddle-point : [[A, Bᵀ],[B, −C]] [u;p] = [f;0]. Résoudre en **direct
  Eigen** (SparseLU) — gère l'indéfini. (MINRES/Uzawa = production, plus tard.)

## Validation — ORACLE Poiseuille (canal plan)
Écoulement entre deux plaques (no-slip) entraîné par gradient de pression ou
force volumique f_z constante. Solution analytique : profil **parabolique**
`u_z(x) = (G/2μ) x (L−x)`, u_max = G L²/(8μ) au centre (G = −dp/dz = f_z).
- Domaine : canal Lx × Ly × Lz, no-slip sur les parois x=0,x=Lx (et y faces selon
  setup ; pour un canal plan, périodique/symétrie en y, ou duc carré avec série —
  privilégier le **canal plan** 1D-analytique : invariance en y,z, parois en x).
- **PASS si** max |u_z_FEM − u_z_analytique| / u_max < 1 % ET pression sans
  oscillation (champ de pression lisse, pas de damier).
- Vérifier la convergence : raffiner (nx=10 → 20) doit réduire l'erreur.

## Livrables
- `src/physics/StokesSolver.{hpp,cpp}` (ou FEMStokes) : assemblage Q1-Q1+PSPG,
  BC (no-slip, force/gradient), solve direct Eigen, accès (u,p).
- Élément : matrices élémentaires (A 24×24 pour u? non : u a 3 comp × 8 nœuds = 24 ;
  p 8 ; coupler en un bloc nodal 32×32, ou blocs séparés A(24×24), B(8×24), C(8×8)).
- `tests/test_stokes.cpp` (CPU pur) : Poiseuille u_max + non-oscillation pression +
  convergence. Cible Makefile CPU-pure, ajoutée à all/test/test_cpu/clean.
- `make` 0 warning. NE PAS committer (le parent vérifie et committe).
- Rapporter : erreur u_max, tendance de convergence, valeur de τ/α_stab, et
  confirmation absence d'oscillation de pression.

## Garde-fous
- Non-dim / unités cohérentes (LL-LIT-012) ; ici μ=1, géométrie unité OK pour l'oracle.
- LL-008 (clamp/borne) ; LL-009 (juger DF sur accord absolu — pas applicable ici,
  oracle analytique direct).
