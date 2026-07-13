# TOOLING A1 — MMA multi-contraintes dans runFluidThermal (volume + T_max + ΔP)

## Objectif
Étendre `runFluidThermal` (TopOptP5/src/apps/topopt_run.cpp) : m ≥ 2 contraintes
simultanées depuis le JSON, avec les adjoints DÉJÀ VALIDÉS DF. Aucun nouveau gate.
CPU double. NE PAS toucher aux classes adjointes ni aux gates.

## Interfaces (vérifiées, code faît foi)
- `MMAOptimizer(int n, int m, const Params&)` + `step(x, f0, df0dx, fvals(m), dfdx(m×n), xmin, xmax)`
  — m≥2 = Newton dual projeté, validé (tests/test_mma.cpp oracleNewton m=2).
- `TripleAdjoint` (objectif J=Fmech^T U + grad) — déjà câblé, ne pas modifier.
- `ThermalObjectiveAdjoint(grid, prm, stokesFixedDofs, bodyForce, thermalDirMask,
  thermalDirVal, Q)` : `objective(gamma)`, `solve(gamma)` → J_T = p-norm(T, poids
  solide 1−γ, P=8) + grad à travers Stokes→CHT. Gate test_tmax_adjoint_fd (7.5e-8).
  VÉRIFIE son struct Params exact (peut différer de TripleAdjoint::Params).
- `DissipationAdjoint(grid, prm, fixedDofs, bodyForce, dirichletVal)` :
  `objective(gamma)` = Φ = ½w^T H w, `solve(gamma)` → grad. Gate 7.2e-7.
  dirichletVal = zéros ici (drive par body force, BCs homogènes).
- Convention γ=1 FLUIDE pour les trois — cohérente, pas de conversion.

## Câblage
1. `runFluidThermal` : construire la liste de contraintes depuis `spec.constraints`
   (types "volume", "tmax", "dissipation" — ProblemSpec les parse déjà en {type,max}).
   Type inconnu → erreur claire. m = taille. `MMAOptimizer mma(ne, m, mp)`.
2. Chaque contrainte normalisée : g = val/max − 1 ≤ 0 ; gradient chaîné dHdT puis
   filt.applyT, MÊME chaîne que le volume existant (lignes ~395–405).
3. tmax/dissipation : instancier les adjoints UNE fois avant la boucle (mêmes BCs
   résolues que TripleAdjoint), appeler solve(gamma) par itération. Coût : +1 solve
   Stokes(+CHT) par contrainte et par itération — acceptable (grilles démo).
4. Rétro-compat : un JSON avec seulement {volume} doit donner un run IDENTIQUE au
   cooling_jacket actuel (m=1, mêmes itérations).
5. Exemple `examples/cooling_jacket_multi.topopt.json` : volume + tmax (+ dissipation),
   bornes choisies pour que tmax soit ACTIF à convergence (serrer T_max sous la valeur
   libre observée ; run préalable pour la mesurer). Documenter le choix.

## Sanity (oracle qualitatif)
- (a) volume seul → identique au run v3 actuel (non-régression bit-à-bit non exigée,
  mais J final et fraction fluide identiques à ~1e-12 près si mêmes seeds/paramètres).
- (b) volume+tmax : les DEUX contraintes actives ou tmax actif + volume actif ;
  J_T final ≤ T_max (faisable) ; T_max serré ⇒ plus de canaux près de la source.
- (c) volume+tmax+dissipation : converge, faisable, rapporter l'ensemble actif.
- Quasi-binaire (grey < 0.15), J décroît.

## Livrables
- topopt_run.cpp étendu + exemple JSON. `make` 0 warning, clean build.
- Non-régression : make test_cpu TOUS verts (les 6 gates inchangés), v1 mbb3d,
  v2 bracket_thermo, v3 cooling_jacket rejouent comme avant.
- NE PAS committer. Rapporter : runs (a)(b)(c) avec J, contraintes finales, ensemble
  actif, itérations, fichiers modifiés.

## Garde-fous
LL-008/009/010. Convention γ=1 fluide. Ne modifier AUCUN fichier src/adjoint/ ni
tests existants. Si une interface réelle diffère de ce spec, le code fait foi —
adapte et signale l'écart dans le rapport.
