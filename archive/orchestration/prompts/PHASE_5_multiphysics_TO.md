# PHASE 5 — Démonstrateur TO multiphysique (MMA + TripleAdjoint + Heaviside)

## Objectif
Boucle d'optimisation multiphysique **de bout en bout** : minimiser
J = LᵀU (compliance à travers la cascade fluide-structure-thermique) sous
contrainte de fraction volumique, pilotée par **MMA** avec le gradient **déjà
validé** de `TripleAdjoint` (gate DF 2.1e-7), filtre de densité 3D + projection
**Heaviside**. Produit un design multiphysique + export VTK. CPU double.

> **Cadrage** : objectif = J=LᵀU (gradient validé, ZÉRO nouveau gate). Les
> contraintes spécifiques cooling jacket (T_max, ΔP, von Mises séparées) = raffinement
> ultérieur (chacune un adjoint-objectif à valider par DF). Ici on démontre le
> PIPELINE complet avec le gradient rigoureux.

## Contexte TopOptP5
- `TripleAdjoint` (src/adjoint) : forward cascade γ→(u,p)→T→U + gradient dJ/dγ validé.
- `MMAOptimizer` (src/topopt) : validé (analytique 6.4e-14). API step(x,f0,df0,fvals,dfdx,xmin,xmax).
- Filtre : il n'y a pas de filtre 3D CPU ; **ajoute un filtre de densité 3D simple**
  (convolution linéaire hat, rayon en cellules, symétrique — comme celui de
  apps/nozzle_axi.cpp mais en 3D) OU réutilise le concept. Chaîne : ρ̃=W ρ, gradient Wᵀ.
- Projection Heaviside : ρ̄ = (tanh(βη)+tanh(β(ρ̃−η)))/(tanh(βη)+tanh(β(1−η))),
  η=0.5, continuation β=1→...→16 sur les itérations. Chaîne de dérivée dρ̄/dρ̃.

## Setup physique (montrer des canaux)
- Domaine 3D (ex. 24×24×32 ou plus modeste selon coût CPU). γ par élément, γ=1 fluide,
  γ=0 solide. Écoulement de coolant entraîné (gradient de pression entrée/sortie).
- **Charge thermique piquée au col** (source Q ou T imposée plus forte à mi-hauteur)
  → l'optimiseur devrait router plus de fluide/canaux près du col.
- Charge mécanique L (pression/force) pour que J=LᵀU soit sensible.
- Optim : **min J=LᵀU s.t. fraction fluide ≤ v_frac** (ou solide ≥ seuil), via MMA.

## Sanity (l'oracle qualitatif de l'intégration)
- **J décroît** de façon (quasi) monotone et converge (pas de divergence).
- **Contrainte volume satisfaite** à convergence (±1%).
- **Design non trivial** : mélange fluide/solide (pas tout-solide ni tout-fluide),
  structure de canaux visible ; Heaviside → ρ̄ proche binaire en fin (peu de gris).
- **Concentration au col** : plus de fluide/canaux près de la charge thermique piquée
  (vérif quantitative : densité fluide moyenne col vs extrémités).

## Livrables
- `src/apps/cooling_jacket.cpp` : boucle MMA (filtre→Heaviside→forward via TripleAdjoint
  →gradient→MMA), sorties console (J, volume, β) + VTK final.
- Filtre 3D (nouveau petit module ou inline). Export `output/cooling_jacket.vti`
  (γ̄ densité + u + T + von Mises via VTKExporter existant).
- Cible Makefile CPU-pure. `make` 0 warning, clean build (LL-010). NE PAS committer.
- Rapporter : courbe J (début→fin), volume final, fraction binaire (gris résiduel),
  ratio densité col/extrémités, capture texte du champ.

## Garde-fous
- LL-008 (clamp ρ avant Heaviside/pow), LL-009, LL-010 (clean build).
- Coût CPU : le forward triple (3 solves directs) × itérations MMA peut être lourd ;
  choisir une taille raisonnable (≤ ~30k DOF total) et ~40-80 itérations. Documenter le temps.
