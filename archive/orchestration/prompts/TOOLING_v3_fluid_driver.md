# TOOLING v3 — dispatch fluide-thermique dans topopt_run (cooling jacket JSON)

## Objectif
Étendre `topopt_run` : quand `physics:["fluid","thermal","elastic"]`, exécuter la
cascade triple complète (Stokes-Brinkman → CHT → thermo-élastique) pilotée par JSON,
via `TripleAdjoint` (gradient VALIDÉ DF 2.1e-7) + MMA + filtre + Heaviside. C'est
paramétrer le démonstrateur `cooling_jacket` depuis le langage d'entrée. Aucun
nouveau gate. CPU double. Complète la vision d'outil (toute la physique depuis JSON).

## Contexte TopOptP5
- `src/apps/cooling_jacket.cpp` : la boucle MMA triple-physique PROUVÉE (min J=Fmech^T U
  s.t. fraction fluide, filtre + Heaviside). **RÉUTILISE sa boucle verbatim** ; ne
  change que la construction des BCs (depuis le spec au lieu de hardcodé).
- `src/adjoint/TripleAdjoint.hpp` : ctor(grid, Params, stokesFixedDofs, bodyForce{3},
  thermalDirMask(nNodes uint8), thermalDirVal(nNodes), Q(nNodes), elasticFixedDofs,
  Fmech(nDof)). solve(gamma)->Solution{J,grad,w,T,U,...}. Params{mu,alphaStab,alphaMax,
  alphaMin,qBrink,ks,kf,E0,Emin,p,alphaTh,Tref,nu}. **CONVENTION : γ=1 FLUIDE, γ=0 SOLIDE**
  (inverse du dispatch v2 structurel — vigilance).
- `src/apps/topopt_run.cpp` : ajoute `runFluidThermal(spec)`, sans casser v1/v2.
- `ProblemSpec`/`BCResolver` : BCs élastiques + thermiques déjà résolues (v1/v2).
  **AJOUTE la résolution des BCs Stokes** (4 DOF/nœud : u_x,u_y,u_z,p) :
  - `bc.flow` faces "wall"/non ouvertes → **no-slip** (fixe u_x,u_y,u_z=0) ;
  - faces de symétrie → slip (fixe une composante) ;
  - un nœud de datum de pression (fixe p) ;
  - `bodyForce` (drive coolant) depuis `bc.flow` (ex. {"drive":[0,0,30]}) ou material.
  Retourne `stokesFixedDofs` (indices dans l'espace 4 DOF/nœud, dof(n,c)=4n+c).

## Setup depuis le JSON
- Grid3D depuis domain.grid. Params depuis material (mu, brinkman_max→alphaMax,
  brinkman_q→qBrink, k_solid→ks, k_fluid→kf, alpha_th→alphaTh, E0/Emin/penal/nu).
- Stokes BCs (nouvelle résolution), bodyForce, thermalDirMask/Val + Q (bc.thermal,
  helpers v2), elasticFixedDofs + Fmech (BCResolver v1).
- Objectif : J=Fmech^T U (via TripleAdjoint, validé). Contrainte : fraction fluide
  ≤ V (volume) ; (optionnel) T_max si `ThermalObjectiveAdjoint` câblable.
- Filtre + Heaviside + MMA depuis le spec.

## Sanity (oracle qualitatif)
- J décroît/converge ; contrainte volume active ; quasi-binaire ; **canaux fluide
  visibles**, plus denses près de la source thermique (comme cooling_jacket, ratio col).
- Export `output/<name>/<name>.vti` (density + speed + temperature + displacement).

## Livrables
- Branche `runFluidThermal` dans `topopt_run.cpp` + résolution BC Stokes dans BCResolver.
- Exemple `examples/cooling_jacket.topopt.json` (physics fluide-thermique-élastique,
  charge thermique piquée, drive coolant, objectif compliance, contrainte volume).
- `make` 0 warning, clean build (LL-010). NE PAS committer. Non-régression : v1 MBB,
  v2 bracket_thermo, make test_cpu restent verts. Rapporter : courbe J, volume,
  canaux/ratio, exemple JSON, fichiers.

## Garde-fous
LL-008/009/010. **Convention γ=1 fluide** (inverse v2) — cohérence critique. Réutiliser
la boucle cooling_jacket et le gradient validé SANS les modifier.
