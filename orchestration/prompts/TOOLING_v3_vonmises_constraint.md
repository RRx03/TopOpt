# TOOLING A3 — contrainte "vonmises" dans runFluidThermal (via solveStress, gate 8.0e-7)

## Objectif
Remplacer le stub `constraint type 'vonmises' is not yet wired` de runFluidThermal
par le câblage réel, avec `TripleAdjoint::solveStress` (gate test_vm_triple_fd
PASS 8.007e-7). Dernière pièce des contraintes combinées v3. Aucun nouveau gate.

## Interfaces (code faît foi)
- `TripleAdjoint::solveStress(gamma, StressParams)` → StressSolution (J_σ + grad,
  cascade complète). `stressObjective(gamma, StressParams)` = valeur seule.
  StressParams : voir TripleAdjoint.hpp (q=0.5, P=8 par défaut).
- Convention γ=1 fluide ; σ relaxé par solidité s=1−γ (déjà interne à solveStress).
- Câblage existant : topopt_run.cpp, boucle m-contraintes générique (volume/tmax/
  dissipation), normalisation g = val/max − 1, chaîne dHdT → filt.applyT.

## Travail
1. Brancher "vonmises" dans la liste de contraintes : g = J_σ/σ_max − 1,
   gradient sol.grad chaîné comme les autres. Un solveStress par itération.
2. Calibration : probe de la valeur libre J_σ sur cooling_jacket (bornes lâches),
   puis borne serrée (~25 % sous la valeur libre) pour un run où vonmises est ACTIF.
3. Exemple `examples/cooling_jacket_full.topopt.json` : volume + tmax + dissipation
   + vonmises — LES QUATRE contraintes. Choix des bornes documenté (_comment).
   Si les quatre simultanément actives sont inatteignables (physiquement possible :
   certaines peuvent dominer), rapporter honnêtement l'ensemble actif réel et
   ajuster pour qu'AU MOINS vonmises + une autre soient actives.
4. Runs : (d) volume+vonmises (vm actif) ; (e) l'exemple full. Rapporter J,
   contraintes finales, ensemble actif, greyness, itérations.

## Livrables
- topopt_run.cpp (stub → réel) + exemple. make 0 warning, make test_cpu tous verts.
- Non-régression : cooling_jacket.topopt.json (volume seul) itérations identiques ;
  cooling_jacket_multi.topopt.json (b/c) inchangé.
- NE PAS committer. Rapport structuré.

## Garde-fous
LL-008/009/010. Ne PAS modifier src/adjoint/ ni tests/. Périmètre : topopt_run.cpp
+ examples/. Si solveStress coûte cher (2 solves cascade/it), c'est accepté (démo).
