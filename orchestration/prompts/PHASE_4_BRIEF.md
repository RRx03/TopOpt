# PHASE_4_BRIEF.md — Couplage thermo-élastique + von Mises + 2D axi

*Brief scientifique opérationnel. Source autoritative pour le travail de Phase 4.
Détail historique : `TopOptP1/TRANSITIONS.md` §Phase 4 (fragilités NIVEAU CRITIQUE).*

---

## OBJECTIF SCIENTIFIQUE

Introduire la première vraie multiphysique : (1) un solveur de conduction
thermique stationnaire, (2) un couplage thermo-élastique faible (dilatation →
contrainte), (3) une contrainte de stress (von Mises < σ_yield) agrégée par
p-norm, (4) une géométrie 2D axisymétrique compatible tuyère. Adjoint multi-bloc,
update MMA.

## JUSTIFICATION DANS LA ROADMAP

C'est le premier saut conceptuel difficile : on quitte la compliance (objectif
lisse, mono-physique, OC suffit) pour des **contraintes ponctuelles agrégées**,
un **adjoint couplé** et un optimiseur plus robuste (**MMA**). On ne peut pas
sauter cette phase pour aller au fluide (Phase 5) : l'adjoint triple-couplé de
Phase 5 n'est qu'une généralisation de l'adjoint deux-blocs validé ici, et la
gestion de la stress singularity et de la p-norm doit être maîtrisée d'abord. La
géométrie 2D axi ancre le projet dans la propulsion (tuyère réelle).

## CONCEPTS MATHÉMATIQUES CENTRAUX

- **Conduction thermique stationnaire** : `−∇·(k(ρ)∇T) = q`, mêmes éléments que
  la mécanique.
- **Couplage thermo-élastique faible (one-way)** : `ε_thermique = α(T−T_ref)` →
  force thermique équivalente `F_thermal` dans l'équilibre élastique.
- **Contrainte de von Mises par élément** + **agrégation p-norm**
  `σ_max ≈ (Σ σ_i^p)^(1/p)`, p=8-12 (Le-Norato-Bruns 2010).
- **Stress singularity & ε-relaxation** (Duysinx-Bendsøe 1998) ou qp-approach
  (Bruggi 2008) : sans quoi σ/ρ → ∞ quand ρ→0.
- **Adjoint multi-bloc** : chaîne ρ→K_méca→U→σ et ρ→K_thermo→T→ε_th→σ.
- **MMA** (Svanberg 1987) : update à asymptotes mobiles, gère multi-contraintes.
- **FEM axisymétrique** (r,z) : intégrales en 2πr, singularité à r=0.

## ACQUIS PRÉREQUIS DE PHASE 3

- [ ] Solveur 3D GPU multi-grid validé, mesh independence démontrée
- [ ] Filtre Helmholtz à rayon physique (mm) opérationnel
- [ ] Pipeline JSON → optim → STL fonctionnel
- [ ] Adjoint compliance (mono-bloc) propre et compris (base de l'extension)

## LIVRABLES SCIENTIFIQUES ATTENDUS

- Solveur thermique stationnaire sur la même grille, validé analytiquement.
- Couplage thermo-élastique faible fonctionnel.
- von Mises + p-norm + ε-relaxation implémentés.
- **Adjoint deux blocs (méca + thermo) validé par DF** sur sub-cas 10×10 (1e-5).
- Update MMA opérationnel (remplace OC pour le multi-contraintes).
- Géométrie 2D axisymétrique (r,z).
- **Cas test tuyère 2D axi** : pression interne 80 bar + flux pariétal 10 MW/m²,
  objectif masse sous `von_Mises < σ_yield` ET `T_paroi < T_max`.
- **Vérification physique** : paroi plus épaisse au col (pic thermo-mécanique).

## PSEUDO-CODE DE L'ALGORITHME CENTRAL

```
for it in [1, ..., n_iter]:
    # Primal couplé thermo-élastique
    T   = solve_thermal(rho, q_wall, T_amb)              # conduction stationnaire
    eps_th = alpha * (T - T_ref)                         # dilatation
    U   = solve_K_elastic(rho) * U = F + F_thermal(eps_th)

    # Évaluation
    sigma_vM = compute_von_mises(U, rho)                 # + eps-relaxation
    sigma_pn = p_norm(sigma_vM, p=8..12)                 # agrégation
    J  = volume(rho)                                     # objectif : masse
    g1 = sigma_pn - sigma_yield                          # contrainte stress
    g2 = T_max(T) - T_max_allowed                        # contrainte thermique

    # Adjoint multi-bloc
    lam_e, lam_t = solve_coupled_adjoint(rho, U, T, J, g1, g2)
    dJ   = volume_sensitivity()
    dg1  = stress_sensitivity(lam_e, rho, U)
    dg2  = thermal_sensitivity(lam_t, rho, T)

    # Filtrage + update
    dJ, dg1, dg2 = map(helmholtz_filter, [dJ, dg1, dg2])
    rho = MMA_step(rho, J, [g1, g2], dJ, [dg1, dg2])
    if converged(rho): break
```

## PIÈGES SPÉCIFIQUES À ANTICIPER (NIVEAU CRITIQUE)

- **LL-LIT-001** — stress singularity : sans ε-relaxation, l'optimiseur refuse de
  mettre du vide. **Implémenter dès la première version des contraintes.** Le bug
  qui fait perdre 2 semaines s'il est découvert tard.
- **LL-LIT-007** — adjoint multi-bloc faux : design "convergent" mais aberrant.
  **Validation par DF OBLIGATOIRE** (10×10, 1e-5) avant tout run grande taille.
- **LL-LIT-011** — asymétrie K_couplée : le couplage one-way T→σ rend la matrice
  non-symétrique ; CG ne marche plus → BiCGStab/GMRES. Décider avant d'assembler.
- **p-norm paramétrage** : p trop petit (<6) sous-estime σ_max ; trop grand (>20)
  gradient non-lisse. Sweet spot 8-12, tester.
- **MMA oscillant** : asymptotes mal mises à jour → GCMMA (Svanberg 2002) en fallback.
- **Singularité axisymétrique à r=0** : nœuds à r=ε>0 ou formulation spéciale.
  Décider et documenter.

## MÉTHODE DE VALIDATION

| Test | Tolérance |
|---|---|
| Patch test thermique (flux uniforme) | < 1e-10 (double) |
| Plaque sous gradient T linéaire vs analytique | < 0.1 % |
| **Adjoint deux blocs par DF (10×10, ε=1e-6)** | **< 1e-5** |
| p-norm vs max réel de σ_vM | écart contrôlé, monotone en p |
| Cas tuyère 2D axi : épaississement au col | présent (sinon bug) |
| Patch test FEM mécanique conservé | < 1e-6 (float) / 1e-10 (double) |

## RÉFÉRENCES BIBLIOGRAPHIQUES

- Pedersen, Pedersen 2010, *Struct. Multidisc. Optim.* 42:681 — thermo-élastique.
- Le, Norato, Bruns 2010, *Struct. Multidisc. Optim.* 41:605 — stress-constrained, p-norm.
- Duysinx, Bendsøe 1998 — ε-relaxation (stress singularity).
- Bruggi 2008 — qp-approach (alternative ε-relaxation).
- Svanberg 1987, *Int. J. Numer. Methods Eng.* 24:359 — MMA. Svanberg 2002 — GCMMA.

## DURÉE ESTIMÉE

8-10 semaines (~10 h/sem).

## DÉCOMPOSITION EN SESSIONS DE TRAVAIL

1. **Solveur thermique seul** (bloc indépendant), patch + plaque analytique.
2. **Couplage thermo-élastique faible** : F_thermal, validation 3D.
3. **von Mises + p-norm + ε-relaxation**.
4. **Adjoint deux blocs + validation DF 10×10** (gate bloquant).
5. **MMA** (remplace OC), tuning convergence.
6. **2D axisymétrique** : élément (r,z), singularité r=0.
7. **Cas tuyère** + vérification physique, rapport + handoff.
