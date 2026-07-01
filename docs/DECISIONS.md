# Decisions — TopOptP5 (Phase 5)

(ADR-001..016 hérités des phases précédentes — voir ../TopOptP4/docs/DECISIONS.md.)

## ADR-017 : Éléments Stokes Q1-Q1 stabilisés PSPG (pas Taylor-Hood)
- **Date** : 2026-06-29
- **Contexte** : Stokes incompressible exige des éléments inf-sup stables
  (Babuška-Brezzi, LL-LIT-002), sinon oscillations de pression / design absurde.
- **Options** : Taylor-Hood (P2 vitesse, P1 pression, stable par construction) ;
  Q1-Q1 égal-ordre + stabilisation PSPG.
- **Décision** : **Q1-Q1 + PSPG**. L'ADN du projet est grille structurée +
  trilinéaire (H8/Q4) + matrix-free/GPU ; Q1-Q1 partage exactement la grille et les
  fonctions de forme (u et p aux mêmes nœuds). Taylor-Hood P2 imposerait des nœuds
  milieux d'arête, cassant l'uniformité structurée et la machinerie existante.
- **Conséquences** : Q1-Q1 viole inf-sup → terme de stabilisation PSPG (résidu de
  moment projeté sur ∇q, paramètre τ ∼ h²/μ) OBLIGATOIRE. À valider sur Poiseuille
  (pas d'oscillation de pression). Le paramètre τ devra être documenté/calibré.

## ADR-018 : Oracles Stokes en CPU double précision (Eigen direct)
- **Date** : 2026-06-29
- **Décision** : comme pour les adjoints P4, valider le solveur Stokes et l'adjoint
  fluide en CPU double (système saddle-point résolu en direct Eigen), pas GPU float32.
- **Conséquences** : oracle propre (Poiseuille, DF adjoint) ; portage GPU
  (MINRES/Uzawa) en production, re-validé contre le chemin CPU.

## ADR-019 : Brinkman penalization Borrvall-Petersson, α_max=1e4
- **Date** : 2026-07-01
- **Décision** : terme α(γ)u dans le moment ; interpolation convexe Borrvall-Petersson
  α(γ)=α_max+(α_min−α_max)γ(1+q)/(γ+q), q=0.1, α_min=0. Sweet spot **α_max=1e4**
  (première valeur avec fuite <1% sur le test de dalle solide), dans [1e3,1e5] (LL-LIT-004).
- **Conséquences** : γ=1 fluide, γ=0 solide ; frontière fluide-solide = variable de
  design. α_max trop grand dégradera le conditionnement (à surveiller en couplage/adjoint).

## ADR-020 : CHT advection-diffusion + SUPG, solveur non-symétrique direct
- **Date** : 2026-07-02
- **Décision** : température CHT `−∇·(k(γ)∇T)+u·∇T=Q`, Q1 scalaire, stabilisation
  SUPG (τ=h/(2|u|)(coth Pe−1/Pe)), système non-symétrique résolu en direct (SparseLU).
- **Conséquences** : advection stabilisée (pas d'oscillation à haut Péclet) ; u pris
  comme champ nodal (couplage à Stokes-Brinkman). k(γ)=k_s+(k_f−k_s)γ.
