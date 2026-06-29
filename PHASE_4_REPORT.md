# Rapport — Phase 4 : thermo-élastique + von Mises + 2D axisymétrique

*Rédigé le 2026-06-29. Valeurs mesurées, CPU double précision pour les oracles.
Le code fait foi. NIVEAU CRITIQUE — le cœur scientifique du projet.*

**Statut** : **Phase 4 substantiellement complète.** Les 6 briques sont en place
et validées (5 gates DF/oracles). Le démonstrateur retenu (tuyère axisymétrique
structurelle) produit la physique attendue. Deux extensions sont explicitement
différées (thermique-en-axisym, démo 3D thermo-élastique complète) — voir §7-8.

---

## 1. But de la phase

Première multiphysique réelle : ajouter la thermique, le couplage thermo-élastique,
les contraintes de stress (von Mises) et la géométrie axisymétrique (tuyère).
Saut conceptuel : on quitte la compliance mono-physique (OC) pour l'**adjoint
multi-bloc**, les **contraintes ponctuelles agrégées** (p-norm), la **stress
singularity** (ε/qp-relaxation) et un optimiseur multi-contraintes (**MMA**).

## 2. Ce qui a été construit (et validé)

| # | Brique | Module | Validation |
|---|---|---|---|
| 1 | Solveur thermique stationnaire | `ThermalSolver` (GPU) | plaque gradient 8.9e-7 |
| 2 | Couplage thermo-élastique faible | `ThermoElasticCoupling`, `H8Element::thermalCoupling` | dilatation libre 7.4e-6 |
| 3 | Stress von Mises + qp-relaxation + p-norm | `StressModel` | von Mises uniaxial 2.4e-15 |
| 4 | **Adjoint 2 blocs (compliance)** | `ThermoElasticAdjoint` | **GATE DF 1.6e-6** |
| 3b | **Sensibilité stress p-norm** | (adjoint étendu) | **GATE DF 1.6e-7** |
| 5 | **MMA** (Svanberg 1987, dual) | `MMAOptimizer` | optimum analytique 6.4e-14 ; OC 0.037 % |
| 6 | **FEM axisymétrique** (r,z) | `Grid2DAxi`, `AxiQ4Element`, `FEM2DAxi` | **Lamé**, ordre 2 (3.84×) |
| 7a | **Adjoint stress axisym** | `AxiStressAdjoint`, `StressModelAxi` | **GATE DF 2.7e-9** |
| 7b | **Tuyère axisym structurelle (TO)** | `nozzle_axi` | physique correcte (cf. §3) |

Tout en C++23, `make` 0 warning, suite `make test_cpu` verte.

## 3. Résultat démonstrateur — tuyère axisymétrique structurelle

Paroi annulaire (a=1, b=1.6, H=4), **pression interne piquée au col** (bosse
gaussienne à mi-hauteur), plane strain. **Masse minimisée sous von Mises p-norm
≤ σ_lim**, pilotée par MMA, sensibilité par `AxiStressAdjoint` (gate 2.7e-9).

| Métrique | Valeur |
|---|---|
| Masse (normalisée) | 0.50 → **0.235** (−53 %) |
| Contrainte stress | active et satisfaite (σ_PN ≈ σ_lim, g = −2e-4) |
| Densité moyenne col vs extrémités | 0.453 / 0.146 = **3.11×** |
| **Verdict physique** | **plus épais au col** (attendu : pic de pression → pic de stress → matière conservée) |

Sortie : `output/nozzle_axi.png` (champ de densité, lignes = z, colonnes = r).
*Démonstrateur STRUCTUREL : pas de thermique (différée, §7).*

## 4. Choix de conception et raisons

### 4.1 Validation systématique par différences finies (5 gates)
Chaque gradient adjoint et chaque optimiseur est validé contre un **oracle
indépendant** avant usage : DF centrées pour les adjoints (compliance, stress 3D,
stress axisym), optimum analytique + cross-check OC pour MMA, Lamé pour l'axisym.
C'est la règle LL-LIT-007, appliquée sans exception. **Aucune optimisation ne
tourne sur un gradient non validé.**

### 4.2 Oracle en CPU double précision
Les gates DF utilisent un chemin CPU double (Eigen direct), pas le GPU float32 :
le bruit float32 masquerait l'erreur d'adjoint (LL-006). Le GPU reste pour la
production grande échelle (à porter, §7).

### 4.3 qp-relaxation de la stress singularity (dès la 1ère version)
`σ_e = ρ_e^q vm0_e` (q=0.5 < p) : la contrainte relaxée → 0 dans le vide, ce qui
lève la singularité (LL-LIT-001). Implémentée d'emblée, pas après coup.

### 4.4 MMA par méthode duale
Sous-problème séparable convexe résolu par le dual (bisection m=1, Newton projeté
m≥2). Optimum analytique atteint à 6.4e-14 → moteur fiable pour le multi-contraintes.

### 4.5 Deux pistes FEM (3D et axisym) — seam assumé (ADR-016)
La machinerie thermo-élastique+stress+adjoint a été bâtie sur la piste 3D ;
l'axisym est une piste FEM distincte (Lamé-validée). Le démonstrateur axisym
réutilise l'adjoint stress *réimplémenté en axisym* (7a). L'unification complète
(thermique en axisym) est différée.

## 5. Pièges rencontrés (→ LESSONS_LEARNED)

- **LL-008** (déjà P3) : `pow(ρ négatif, p non entier)` = NaN → boucle OC infinie ;
  clamp ρ avant tout pow, borner toute bissection. Re-appliqué partout en P4.
- **LL-009** (nouveau) : plancher d'arrondi en validation DF — juger sur l'accord
  absolu (chiffres significatifs), pas l'erreur relative sur gradients quasi-nuls ;
  le sens de variation avec ε distingue arrondi (décroît) de bug (croît).
- **Axisym** : la matrice élémentaire dépend de r → KE0ax **propre à chaque
  élément**, pas de matrice partagée (erreur classique évitée, vérifiée par le gate).

## 6. Validations (récap chiffré)

| Test | Cible | Obtenu |
|---|---|---|
| Patch thermique / plaque gradient | < 1e-6 | 8.9e-7 |
| Dilatation libre thermo-élastique | exact | 7.4e-6 |
| von Mises uniaxial | exact | 2.4e-15 |
| Adjoint compliance DF | < 1e-5 | 1.6e-6 |
| Adjoint stress p-norm DF | < 1e-5 | 1.6e-7 |
| MMA optimum analytique | < 1e-3 | 6.4e-14 |
| MMA vs OC (MBB) | ±5 % | 0.037 % |
| Lamé axisym (σ_θ, nr=40) | < 2 % | 5.3e-5 (ordre 2) |
| Adjoint stress axisym DF | < 1e-5 | 2.7e-9 |
| Tuyère : épaississement au col | présent | ratio 3.11× |

## 7. Dette technique / différé (assumé, vers l'outil complet 3D+thermique)

| Item | Raison | Pour la suite |
|---|---|---|
| **Thermique en axisymétrique** | démonstrateur structurel suffisant pour cette étape | porter ThermalSolver+couplage+adjoint sur Grid2DAxi (+ gate DF axisym thermo) |
| **Démo 3D thermo-élastique complète** | la stack 3D est validée mais le cas n'est pas assemblé | assembler un cas 3D pressurisé+chauffé, mass-min sous von Mises+T_max via MMA (zéro nouveau gate — tout est validé) |
| **Portage GPU des adjoints** | adjoints validés en CPU double (oracle) | matrix-free float32, re-validé contre le chemin CPU |
| **Unification des 2 pistes (3D/axi)** | ADR-016 | architecture commune élément/adjoint à terme |

## 8. Prompt d'approfondissement (pour le travail différé)

> Pour qui reprend l'extension thermique-axisym ou la démo 3D complète :

```
Contexte : TopOptP4 a, validé par différences finies (<1e-5), un adjoint
thermo-élastique 2 blocs (compliance + stress p-norm) sur la piste 3D
(ThermoElasticAdjoint), et un adjoint stress élastique sur la piste
axisymétrique (AxiStressAdjoint). MMA (validé) est l'optimiseur. Deux cibles :

CIBLE A — Tuyère axisym THERMO-élastique complète :
 1. Porter la conduction thermique sur Grid2DAxi (Laplacien axisym k(rho), même
    structure que ThermalSolver mais en (r,z), facteur r).
 2. Porter le couplage thermo-élastique (force thermique axisym = ∫ B^T D m alpha
    (T-Tref) r dA) et l'étendre dans AxiStressAdjoint -> adjoint 2 blocs axisym.
 3. NOUVEAU GATE : valider dsigma_PN/drho et d(compliance)/drho thermo-axi par DF
    centrées (<1e-5) sur ~10x10 avant toute optimisation.
 4. Cas tuyère : pression interne + flux thermique pariétal au col, mass-min sous
    von Mises + T_max via MMA. Vérifier épaississement au col renforcé par la
    contrainte thermique.

CIBLE B — Démo 3D thermo-élastique (aucun nouveau gate, tout est validé) :
 1. Assembler un cas 3D : bloc/conduit pressurisé + chauffé, BCs réalistes.
 2. Boucle MMA : mass-min sous von Mises p-norm (gate 1.6e-7) + T_max, gradients
    par ThermoElasticAdjoint (gate 1.6e-6). Filtre Helmholtz mm (P3).
 3. Porter les solves sur GPU matrix-free (P2/P3) pour la grande échelle ;
    re-valider l'adjoint GPU float32 contre le chemin CPU double.
Détaille le pseudo-code, les BCs, et le critère de convergence MMA.
```

## 9. Décisions architecturales (docs/DECISIONS.md)
ADR-005..016. Clés de Phase 4 : ADR-013 (qp-relaxation+p-norm), ADR-014 (adjoint
unifié compliance/stress), ADR-015 (MMA dual), ADR-016 (seam 3D/axisym).

## 10. Checklist de clôture
- [x] 6 briques implémentées, 5 gates DF/oracles verts
- [x] Démonstrateur tuyère axisym : physique correcte (épaississement au col)
- [x] `make` 0 warning ; `make test_cpu` vert
- [x] `LESSONS_LEARNED.md` : LL-008 ré-appliqué, LL-009 ajouté
- [x] `docs/DECISIONS.md` : ADR-013..016
- [x] `handoffs/PHASE_4_TO_5.md` produit
- [x] Différés documentés (thermique-axi, démo 3D, portage GPU adjoint)
