# PHASE 5R — Reproduction Borrvall-Petersson (validation vs littérature)

## Objectif
Reproduire un cas canonique de TO fluide de **Borrvall-Petersson 2003** avec le
gradient de dissipation **validé** (`DissipationAdjoint`, gate DF 7e-7) + MMA +
filtre. Comparer le design et la dissipation au résultat publié. C'est le livrable
« notre solveur reproduit un résultat publié ». CPU double.

## Cas retenu — le DIFFUSEUR (BP 2003, exemple canonique)
- Domaine carré quasi-2D [0,1]×[0,1]×(slab mince z, slip faces z).
- **Entrée** : bord gauche, profil de vitesse parabolique sur toute la hauteur,
  débit Q entrant (u_x>0, u_y=0).
- **Sortie** : bord droit, ouverture réduite (1/3 hauteur, centrée), reste no-slip.
- **Parois** : no-slip sur haut/bas et les portions non-ouvertes.
- Conservation du débit entrée=sortie (la sortie plus étroite → accélération).
- Objectif : **min Φ (dissipation)** s.t. **fraction fluide ≤ V** (ex. V=0.5).
- Résultat BP attendu : un **canal lisse convergent** reliant l'entrée large à la
  sortie étroite (pas de coude parasite, pas de damier). Design quasi-binaire.

## Pipeline (composer le validé)
- `DissipationAdjoint::solve(gamma)` → Φ + dΦ/dγ (validé). Filtre densité 3D (réutilise
  celui de `cooling_jacket.cpp`). (Heaviside optionnel pour binariser en fin.)
- MMA : min Φ s.t. volume. Continuation Brinkman α_max possible (1e2→1e4) pour éviter
  les minima locaux (LL-LIT-004). ~50-100 itérations.

## Sanity + comparaison (l'oracle)
- Φ décroît, converge ; contrainte volume active (±1%).
- **Topologie** : un canal convergent lisse se forme (entrée→sortie), pas de damier,
  quasi-binaire. C'est la signature BP.
- **Quantitatif si possible** : rapporter Φ_optimal et le comparer à l'ordre de grandeur
  BP ; vérifier que Φ_optimal < Φ(design uniforme γ=V) (l'optim améliore vraiment).
- Export `output/bp_diffuser.vti` (densité + vitesse + pression, ParaView) + une coupe
  PNG du canal.

## Livrables
- `src/apps/bp_diffuser.cpp` : setup diffuseur + boucle MMA + export VTK/PNG.
- Cible Makefile CPU-pure. `make` 0 warning, clean build (LL-010). NE PAS committer.
- Rapporter : courbe Φ, volume final, description de la topologie obtenue (canal ?),
  Φ_opt vs Φ_uniforme, temps, et **comparaison honnête au résultat BP** (ressemblance
  du canal, ordre de grandeur). Si la topologie ne ressemble PAS à BP, ne maquille
  pas : décris ce qui sort et diagnostique.

## Garde-fous
LL-008/009/010. Non-régression des tests existants.
