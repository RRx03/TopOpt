# TOOLING v2 — dispatch thermo-élastique dans topopt_run (JSON multiphysique)

## Objectif
Étendre `topopt_run` : quand `physics:["thermal","elastic"]`, exécuter une
optimisation **masse-min sous contrainte von Mises** (± T_max) pilotée par le JSON,
via `ThermoElasticAdjoint` (gradient stress p-norm VALIDÉ DF 1.6e-7) + MMA + filtre
+ Heaviside. Sortie VTK. **Clôt aussi le différé P4 « démo 3D thermo-élastique
complète ».** CPU double (ThermoElasticAdjoint est CPU). Aucun nouveau gate.

## Contexte TopOptP5
- `src/apps/topopt_run.cpp` : driver existant (v1 structurel GPU). Ajoute une branche
  thermo-élastique (CPU) sans casser v1.
- `ThermoElasticAdjoint` (src/adjoint) : Material{E0,Emin,p,k0,kmin,q,alpha,Tref,nu} ;
  constructeur(grid, mat, elasticFixedDofs, bodyForce?, thermalDirMask, thermalDirVal,
  Q, elasticFixedDofs2, Fmech) — LIS l'en-tête pour la signature exacte ; méthodes
  `objective(gamma)`, `stressPNorm(gamma, StressModel)`, `stressPNormGrad(gamma, sm)`
  → Solution{J, grad, ...}. Gradient stress VALIDÉ (ne pas revalider).
- `StressModel` (src/topopt/StressModel.hpp) : von Mises + qp-relax + p-norm.
- `MMAOptimizer` (validé), filtre densité 3D + Heaviside (cf. cooling_jacket.cpp).
- `ProblemSpec`/`BCResolver` : BCs élastiques déjà résolues ; ajoute la résolution
  des BC thermiques (`bc.thermal` : face→Dirichlet T, region→source Q) — sélecteurs
  faces comme BCResolver, retourne thermalDirMask/thermalDirVal (nNodes) et Q (nElems).

## Formulation (mass-min stress-constrained)
- Design γ par élément (ici γ=1 matière, γ=0 vide — convention structurelle ; ATTENTION
  ThermoElasticAdjoint peut utiliser une convention interne, VÉRIFIE dans l'en-tête et
  reste cohérent).
- Objectif : **masse** = (Σ ρ̄_e)/n (gradient trivial, chaîné filtre+Heaviside).
- Contrainte : `g1 = σ_PN(ρ̄)/σ_lim − 1 ≤ 0` (gradient via `stressPNormGrad`, chaîné).
  σ_lim depuis `constraints[type=vonmises].max`.
- (optionnel) `g2 = T_max/T_lim − 1` si `constraints[type=tmax]` présent (utilise
  `ThermalObjectiveAdjoint` si dispo, sinon documente non-supporté en v2).
- MMA multi-contraintes, continuation Heaviside β (spec.filter.heaviside.beta),
  ε-relaxation stress (déjà dans StressModel), max_iter depuis le spec.

## Setup depuis le JSON
- Grid3D depuis `domain.grid`. BCs élastiques (fixed/loads/pressure) via BCResolver.
- BCs thermiques via la nouvelle résolution (`bc.thermal`).
- Matériau depuis `material`. Filtre rayon mm.

## Sanity (oracle qualitatif)
- Masse décroît/converge ; **contrainte von Mises active** (σ_PN ≈ σ_lim à convergence).
- Quasi-binaire (Heaviside). Design non trivial (matière là où la contrainte l'exige).
- Export `output/<name>/<name>.vti` (densité + von Mises + température + déplacement).

## Livrables
- Branche thermo-élastique dans `topopt_run.cpp` (+ résolution BC thermiques dans
  `BCResolver` ou un helper). Exemple `examples/bracket_thermo.topopt.json`.
- `make` 0 warning, clean build (LL-010). NE PAS committer. Non-régression : v1 (MBB)
  et tous les tests restent verts. Rapporter : courbe masse, σ_PN vs σ_lim, design.

## Garde-fous
LL-008/009/010. Réutiliser les gradients validés SANS les modifier. Si une convention
de densité diffère (γ fluide vs solide), la documenter et rester cohérent.
