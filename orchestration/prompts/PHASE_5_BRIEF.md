# PHASE_5_BRIEF.md — Couplage Stokes + CHT, TO multiphysique complète

*Brief scientifique opérationnel. Source autoritative pour le travail de Phase 5.
Détail historique : `TopOptP1/TRANSITIONS.md` §Phase 5 (fragilités TRÈS CRITIQUES).*

---

## OBJECTIF SCIENTIFIQUE

Le livrable phare : ajouter le fluide (Stokes incompressible) avec Brinkman
penalization et couplage CHT (Conjugate Heat Transfer), pour optimiser une
chemise de refroidissement régénératif. Adjoint **triple-couplé**
(méca + thermo + Stokes).

## JUSTIFICATION DANS LA ROADMAP

C'est l'aboutissement différenciant du projet (vraie TO multiphysique, ce que
Noyron/Leap71 ne font pas). Le défi est autant conceptuel qu'algorithmique : la
**frontière fluide-solide devient une variable de design**, gérée par Brinkman
penalization (le solide est un fluide infiniment visqueux). L'adjoint passe à
trois blocs couplés — la généralisation directe de l'adjoint deux-blocs validé en
Phase 4. Sans les Phases 3 (rapidité/mesh independence) et 4 (adjoint multi-bloc,
contraintes, MMA), cette phase est ingérable.

## CONCEPTS MATHÉMATIQUES CENTRAUX

- **Stokes incompressible** : `−μ∇²u + ∇p = f`, `∇·u = 0` (saddle-point indéfini).
- **Brinkman penalization** : terme `α(ρ)u` dans le moment, `α(ρ) = α_max(1−ρ)^q`
  (q=4-8), pousse u→0 dans le solide.
- **Éléments stables (inf-sup / Babuška-Brezzi)** : Taylor-Hood (P2-P1) ou Q1-Q1
  stabilisé PSPG.
- **Solveur saddle-point** : MINRES, complément de Schur, ou Uzawa préconditionné
  (block-diagonal masse + Laplacien). CG ne marche pas (matrice indéfinie).
- **CHT** : conduction dans le solide + advection-diffusion dans le fluide,
  continuité de T et du flux à l'interface (grille commune).
- **Adjoint triple-couplé** : (∂R_méca, ∂R_thermo, ∂R_Stokes), résolution par
  blocs ou Newton-Krylov.
- **Filtre projection Heaviside** (Wang-Lazarov-Sigmund 2011) : binarise ρ, évite
  les microcanaux mal pénalisés.
- **Non-dimensionnalisation** : échelles de référence avant assemblage.

## ACQUIS PRÉREQUIS DE PHASE 4

- [ ] Solveur thermique + couplage thermo-élastique validés
- [ ] von Mises + p-norm + ε-relaxation opérationnels
- [ ] **Adjoint deux blocs validé par DF** (la méthode, à généraliser)
- [ ] MMA opérationnel et stable
- [ ] Cas tuyère 2D axi produisant un design physiquement correct

## LIVRABLES SCIENTIFIQUES ATTENDUS

- Solveur Stokes + Brinkman, éléments stables (Taylor-Hood ou Q1-Q1 PSPG).
- Solveur saddle-point (MINRES/Uzawa) préconditionné.
- Couplage CHT (conduction solide + advection-diffusion fluide).
- **Adjoint triple-couplé validé par DF** sur sub-cas 8×8 (1e-3).
- Filtre projection Heaviside pour binarisation.
- Non-dimensionnalisation complète, résidu non-dim contrôlé.
- **Cas application** : cooling jacket régénératif pour tuyère méthalox 5 kN,
  contraintes `T_paroi < T_max`, `ΔP < ΔP_max`, `von_Mises < σ_yield`,
  objectif masse.
- **Vérification qualitative** : canaux longitudinaux à section variable, plus
  densément groupés près du col — **sans les encoder explicitement**.
- Reproduction qualitative d'un cas Borrvall-Petersson (channel) à ~5 %.

## PSEUDO-CODE DE L'ALGORITHME CENTRAL

```
for it in [1, ..., n_iter]:
    # Primal triple
    u, p = solve_stokes_brinkman(rho, alpha_max)        # saddle-point (MINRES)
    T    = solve_advection_diffusion(u, rho, q_in)      # CHT
    U    = solve_K_elastic(rho, T) = F_pressure(p)      # élasticité

    # Évaluation
    J  = volume(rho)                                    # objectif : masse
    g1 = T_max_solid(T) - T_max_allowed
    g2 = dP(p_in, p_out) - dP_max
    g3 = sigma_vM_max(U, rho) - sigma_yield

    # Adjoint triple-couplé (résolu en backward)
    lam_e = solve_adjoint_elastic(...)
    lam_t = solve_adjoint_thermal(...)
    lam_s = solve_adjoint_stokes(...)

    # Gradients composés
    dJ   = volume_sensitivity()
    dg_i = compose(lam_e, lam_t, lam_s)                 # chaîne complète

    # Filtrage (Helmholtz) + projection (Heaviside) + update
    update_via_MMA(rho, J, [g1, g2, g3], dJ, dg_i)
    if converged(rho): break
```

## PIÈGES SPÉCIFIQUES À ANTICIPER (NIVEAU TRÈS CRITIQUE)

- **LL-LIT-002** — inf-sup : mauvais éléments → oscillations de pression, design
  absurde. **Décider Taylor-Hood vs Q1-Q1 PSPG AVANT de coder une ligne de Stokes**,
  documenter dans `docs/DECISIONS.md`.
- **LL-LIT-012** — non-dimensionnalisation : mélange Pa/m·s⁻¹/K/MPa → cond > 1e15,
  divergence silencieuse. **Définir les échelles avant le premier assemblage.**
- **LL-LIT-004** — Brinkman mal calibrée : α_max trop faible → fuite ; trop grand →
  conditionnement explose. Sweet spot 1e3-1e5, continuation progressive.
- **LL-LIT-007** — adjoint triple-couplé : le point le plus dur du projet.
  **DF sur 8×8 à 1e-3 ; si ça ne matche pas, STOP, débugger, ne pas avancer.**
- **Décollement de canaux** : microcanaux que Brinkman ne pénalise plus → filtre
  Heaviside obligatoire.
- **Coût** : ~30 min/itération sur 128³ (3 primaux + 3 adjoints). Démos à
  résolution réduite ; documenter la limite.

## MÉTHODE DE VALIDATION

| Test | Tolérance |
|---|---|
| Poiseuille (Stokes) vs analytique | u_max < 0.5 % |
| Inf-sup : pas d'oscillation de pression sur grille uniforme | qualitatif |
| CHT 1D advection-diffusion vs analytique | < 1 % |
| Brinkman : pas de fuite fluide dans le solide | qualitatif |
| **Adjoint triple par DF (8×8)** | **< 1e-3** |
| Reproduction Borrvall-Petersson (channel) | ~5 % |
| Conditionnement matrices non-dim | cond < 1e12 |
| Cooling jacket : canaux concentrés au col | présent (sinon bug/spec) |

## RÉFÉRENCES BIBLIOGRAPHIQUES

- Borrvall, Petersson 2003, *Int. J. Numer. Methods Fluids* 41:77 — Brinkman, cas test.
- Dilgen et al. 2018, *Struct. Multidisc. Optim.* 57:1905 — TO multiphysique fluide-thermique adjointe.
- Alexandersen, Andreasen 2020, *Fluids* 5(1):29 — review TO fluide (lecture obligatoire).
- Wang, Lazarov, Sigmund 2011 — filtre projection Heaviside.
- Causin, Gerbeau, Nobile 2005 — added-mass instability (si extension transitoire).

## DURÉE ESTIMÉE

10-14 semaines (~10 h/sem).

## DÉCOMPOSITION EN SESSIONS DE TRAVAIL

1. **Stokes seul** : décision éléments (inf-sup), non-dim, validation Poiseuille.
2. **Solveur saddle-point** (MINRES/Uzawa) préconditionné.
3. **Brinkman penalization** : α(ρ), continuation, test de non-fuite.
4. **CHT** : advection-diffusion couplée, validation analytique.
5. **Adjoint triple-couplé + DF 8×8** (gate bloquant absolu).
6. **Heaviside + intégration MMA** multi-contraintes.
7. **Cooling jacket** : cas méthalox 5 kN, vérif qualitative, comparaison Borrvall-Petersson.
8. **Clôture** : rapport, limitations documentées, handoff vers Phase 6.
