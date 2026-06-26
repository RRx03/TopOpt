# PHASE_3_BRIEF.md — Multi-grid uniforme + mesh independence

*Brief scientifique opérationnel. Source autoritative pour le travail de Phase 3.
Détail historique : `TopOptP1/TRANSITIONS.md` §Phase 3.*

---

## OBJECTIF SCIENTIFIQUE

Construire une hiérarchie de grilles uniformes (restriction/prolongation +
warm-start) et démontrer la **mesh independence** via un filtre Helmholtz à rayon
physique (mm). Aucune physique nouvelle : on rend le solveur 3D rapide et fiable.

## JUSTIFICATION DANS LA ROADMAP

La TO converge lentement sur grille fine (gradient bruité par les détails) et
vite sur grille grossière (mais design grossier). La stratégie hiérarchique
exploite les deux : optimiser d'abord en grossier, interpoler, raffiner. Sans
cette phase, le 3D de Phase 2 reste trop lent pour itérer et son design **dépend
du maillage** — défaut rédhibitoire. La **mesh independence** est *la* propriété
qui distingue un solveur TO mature d'une démo de TP. Elle est aussi un prérequis
de confiance pour la multiphysique (Phases 4-5) : inutile de coupler des physiques
si le design de base bouge avec la résolution.

## CONCEPTS MATHÉMATIQUES CENTRAUX

- **Opérateurs inter-grilles** : prolongation (interpolation grossier→fin) et
  restriction (fin→grossier, transposée pondérée). Interpolation **conservative**
  (moyenne pondérée par volume) pour préserver la fraction volumique.
- **Warm-start** : ρ du niveau fin initialisé par prolongation du niveau grossier
  convergé.
- **Filtre Helmholtz à rayon physique** : r exprimé en mm, `r_cells = r_mm/h`,
  donc indépendant de la résolution (Lazarov-Sigmund 2011).
- **(Optionnel) Préconditionneur multigrid V-cycle** : smoother (Jacobi ou
  red-black Gauss-Seidel), coarsening, prolongation/restriction, pour accélérer le
  CG du primal. Fallback acceptable : CG+Jacobi+warm-start.

## ACQUIS PRÉREQUIS DE PHASE 2 (vérifier réellement, pas par le nom des fichiers)

> ⚠️ **État actuel : Phase 2 ≈ 1/9 (fondation Metal seule).** Les prérequis
> ci-dessous **ne sont PAS satisfaits aujourd'hui**. Phase 3 ne peut pas démarrer
> tant que le solveur 3D GPU n'est pas complet et validé. Cf.
> `TopOptP2/PHASE_2_REPORT.md` et `handoffs/PHASE_2_TO_3.md`.

Prérequis bloquants :
- [ ] Élément H8 + assembly K en CSR sur GPU, validé (K_GPU ≈ K_CPU à 1e-6 float)
- [ ] SpMV creux GPU validé
- [ ] CG préconditionné Jacobi GPU convergent (résidu < 1e-6 en < 2000 iter)
- [ ] Patch test FEM 3D (< 1e-6 float), cantilever 3D analytique (< 2 %)
- [ ] Loop TO 3D complète, MBB 3D, export STL
- [ ] Benchmark 128³ < 10 min

## LIVRABLES SCIENTIFIQUES ATTENDUS

- Hiérarchie 3-4 niveaux (ex. 32³ → 64³ → 128³ → 256³) fonctionnelle.
- Opérateurs prolongation/restriction testés (round-trip ≈ identité).
- Filtre Helmholtz à rayon physique (mm) configurable.
- **Démonstration de mesh independence** : design MBB 3D à 64³ ≈ 128³ ≈ 256³
  (mêmes features, à raffinement près), avec **r en mm fixe**.
- **Speedup 5-10×** sur l'optimisation complète vs Phase 2 single-grid (mesuré).
- Test de sensibilité au rayon : r=1mm → features fines, r=2mm → features grosses,
  variation **prévisible**.
- Pipeline JSON → optim multi-grid → STL end-to-end.

## PSEUDO-CODE DE L'ALGORITHME CENTRAL

```
for level in [coarse, ..., fine]:
    grid_l = build_grid(level)
    if level == coarse:
        rho_l = initialize_uniform(grid_l, V_target)
    else:
        rho_l = prolongate(rho_{l-1}, grid_l, conservative=True)   # warm-start

    for it in [1, ..., n_iter_l]:
        U_l   = solve_K(rho_l) * U_l = F            # primal (CG GPU, +V-cycle?)
        J_l   = compute_compliance(U_l)
        dJ    = compute_adjoint_gradient(rho_l, U_l)
        dJ_f  = helmholtz_filter(dJ, r_physical_mm) # rayon en mm
        rho_l = OC_update(rho_l, dJ_f, V_target_l)
        if converged(rho_l): break

    validate_volume(rho_l, V_target)                # tol 0.01 %
```

## PIÈGES SPÉCIFIQUES À ANTICIPER

- **LL-LIT-006** — rayon en cellules au lieu de mm : toute la mesh independence
  échoue. Implémenter le rayon physique dès le premier commit du filtre.
- **LL-LIT-010** — interpolation non-conservative : dérive de volume entre niveaux.
  Tester la conservation à 0.01 % avant/après chaque transition.
- **Continuation de p entre niveaux** : repartir p=1 (safe, cher) ou hériter le p
  final du niveau précédent (rapide, risque de minimum local) ? **Décider et
  documenter dans `docs/DECISIONS.md` avant de coder la loop.**
- **Mauvais minimum grossier propagé** : un design 64³ médiocre contamine le
  warm-start 128³. Diagnostiquer via plusieurs init (uniforme/aléatoire/gradient).
- **V-cycle non convergent** : fallback CG+Jacobi+warm-start acceptable, mais
  **documenter** le choix.

## MÉTHODE DE VALIDATION

| Test | Tolérance |
|---|---|
| Round-trip prolongation∘restriction sur champ lisse | < 1e-6 (double) / 1e-4 (float) |
| Conservation de volume entre niveaux | < 0.01 % |
| Mesh independence MBB 3D (64³ vs 128³ vs 256³, r mm fixe) | features identiques visuellement |
| Speedup vs Phase 2 single-grid | ≥ 5× (cible 5-10×) |
| Sensibilité au rayon (r=1mm vs 2mm) | variation prévisible des features |
| Patch test FEM 3D conservé | < 1e-6 (float) |

## RÉFÉRENCES BIBLIOGRAPHIQUES

- Aage, Andreassen, Lazarov 2015, *Struct. Multidisc. Optim.* 51:565 — multigrid + large-scale.
- Aage et al. 2017, *Nature* 550 — large-scale TO, préconditionnement multigrid.
- Lazarov, Sigmund 2011, *Int. J. Numer. Methods Eng.* 86:765 — filtre Helmholtz physique.

## DURÉE ESTIMÉE

4-6 semaines (~10 h/sem).

## DÉCOMPOSITION EN SESSIONS DE TRAVAIL

1. **Hiérarchie + opérateurs** : `Grid3DMultiLevel`, prolongation/restriction GPU,
   test round-trip + conservation volume.
2. **Filtre physique** : Helmholtz à rayon mm, test de sensibilité au rayon.
3. **Loop multi-grid + warm-start** : décision continuation de p, intégration,
   conservation inter-niveaux.
4. **Mesh independence + benchmark** : MBB 3D à 3 résolutions, mesure du speedup,
   (optionnel) V-cycle.
5. **Clôture** : pipeline JSON→STL end-to-end, rapport + handoff.
