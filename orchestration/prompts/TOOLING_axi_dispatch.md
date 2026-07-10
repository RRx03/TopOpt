# TOOLING B — dispatch axisymétrique dim:"axi" dans topopt_run (tuyère profilée JSON)

## Objectif
Brancher `dim:"axi"` + `physics:["elastic"]` dans topopt_run : reproduire le
démonstrateur nozzle_profiled (masse min sous contrainte von Mises, alésage
convergent-divergent, charge pression profilée) entièrement depuis JSON.
Aucun nouveau gate (AxiStressAdjoint validé DF < 1e-5). CPU double.

## Briques (toutes existantes et validées — code faît foi)
- `Grid2DAxi(nr, nz, a, b, H)` + `setNodeRadii(rmap)` (grille body-fitted).
- `FEM2DAxi(grid, nu)` : setFixedDofs, solve(E,F), `pressureLoadInnerProfiled(pAtRow)`
  (bit-exact vs rectangulaire, test_axi_mapped).
- `AxiStressAdjoint(grid, Material{E0,Emin,p,nu}, fixedDofs, F)` :
  stressPNorm / stressPNormGrad (gate 1e-5). Convention rho=1 matière.
- `StressModelAxi(nu, qRelax, Pagg)`.
- `src/apps/nozzle_profiled.cpp` : la boucle PROUVÉE (Nozzle{H,rThroat,K,wall},
  nozzleNodeRadii, poids volume Pappus rc·area, MMA move=0.12, Heaviside β 1→4,
  garde-fou sanité Lamé). RÉUTILISE sa logique verbatim ; seuls les paramètres
  viennent du spec.

## Travail
1. **ProblemSpec** : parser `domain.nozzle` → struct NozzleParams{rThroat, K, wall}
   (H = size_mm[1] ou champ dédié — choisir et documenter). `dim:"axi"` : grid[0]=nr,
   grid[1]=nz, grid[2] ignoré. Champ absent → défauts du démonstrateur.
2. **BCs axi** : pas de BCResolver2D complet — résolution directe dans runAxiStruct :
   - `bc.fixed` face "z-"/"z+" dof "z" → plane strain u_z=0 (comme le démonstrateur) ;
   - `bc.pressure` face "inner"/"r-" value=p_peak → charge profilée piquée à la gorge
     (même profil que nozzle_profiled). Sélecteur non supporté → erreur claire.
3. **runAxiStruct(spec)** : grille mappée depuis NozzleParams (geometry=="nozzle") ou
   rectangulaire (geometry=="box", a/b depuis size_mm — documenter la convention),
   boucle masse-min/von Mises de nozzle_profiled paramétrée par le spec
   (σ_lim relatif au design solide : contrainte {type:"vonmises","max_rel":1.6} ou
   "max" absolu — choisir, documenter dans INPUT_LANGUAGE au rapport), filtre 2D,
   Heaviside depuis spec.heaviside_beta, MMA.
4. **Dispatch main()** : branche axi AVANT le message d'erreur. dim=="axi" avec
   physics≠["elastic"] → erreur claire (non supporté).
5. **Sorties** : PNG cross-section (canonique — axe gris à r=0 hors domaine),
   VTK 3D révolu + STL révolu comme le démonstrateur, dans output/<name>/.
6. **Exemple** `examples/nozzle_profiled.topopt.json` reproduisant EXACTEMENT le
   démonstrateur (mêmes paramètres). Sanity : masse finale, σ_PN/σ_lim, greyness
   comparés au run du démonstrateur nozzle_profiled (mêmes valeurs à convergence —
   idéalement identiques si mêmes seeds/itérations).

## Livrables
- ProblemSpec étendu, runAxiStruct, dispatch, exemple JSON. make 0 warning.
- make test_cpu tous verts ; non-régression v1/v2/v3 JSON (mbb3d, bracket_thermo,
  cooling_jacket) + exemples multi récents inchangés.
- NE PAS committer. Rapport : comparaison démonstrateur vs JSON (masse, σ, itérations),
  fichiers, choix de schéma (max_rel etc.), écarts.

## Garde-fous
LL-008/009/010. Ne PAS modifier src/adjoint/, src/fem/, src/core/, tests/ existants.
Convention rho=1 matière (chemin structurel). Le PNG cross-section fait foi pour
l'orientation (leçon nozzle : bord gauche = paroi interne r=a, PAS l'axe).
