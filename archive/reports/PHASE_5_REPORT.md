# Rapport — Phase 5 : Stokes + CHT, TO multiphysique (le livrable phare)

*Rédigé le 2026-07-08. Valeurs mesurées, CPU double précision (oracles). Le code
fait foi. NIVEAU TRÈS CRITIQUE — l'aboutissement différenciant du projet.*

**Statut** : **Phase 5 substantiellement complète.** Toute la physique fluide
(Stokes+Brinkman), le CHT, l'**adjoint triple-couplé** (le gate le plus dur du
projet) et le **démonstrateur TO multiphysique de bout en bout** sont en place et
validés. Le cœur scientifique du projet — TO multiphysique fluide-structure-thermique
par gradient adjoint — est **validé de bout en bout**.

---

## 1. But de la phase

Ajouter le fluide (Stokes incompressible + Brinkman penalization) et le couplage
CHT (conjugate heat transfer), pour optimiser par gradient adjoint un système
fluide-structure-thermique complet — la chemise de refroidissement d'une tuyère.
C'est ce qui différencie le projet de Noyron/Leap71 (KBE) : une **vraie** TO
multiphysique adjointe, que peu d'acteurs intègrent à ce niveau.

## 2. Ce qui a été construit (et validé)

| # | Brique | Module | Validation (oracle) |
|---|---|---|---|
| 1 | **Stokes** Q1-Q1 PSPG | `StokesSolver` | Poiseuille force (u exact) + **pression-pilotée** (couplage B/Bᵀ, u O(h²), p linéaire) ; inf-sup confirmé |
| 2 | **Brinkman** α(γ)u | `StokesSolver::setBrinkman` | **Darcy-Brinkman 1D** (cosh) relErr 1.2e-3 O(h²) ; non-fuite α_max=1e4 (0.47%) |
| 3 | **CHT** advection-diffusion | `CHTSolver` | conduction exacte 4.4e-15 ; adv-diff 1D O(h²) ; **piège Péclet** (Galerkin oscille, SUPG propre) |
| 4 | **Adjoint triple-couplé** | `TripleAdjoint` | **GATE DF 2.1e-7** (tol 1e-3), 3 termes actifs |
| 5 | **Démonstrateur TO** | `cooling_jacket` | J −71% convergé, volume actif, quasi-binaire, ratio col 2.16× |

Optimiseur : **MMA** (validé P4). Tout en C++23, CPU double (oracles), `make` 0 warning.

## 3. Le résultat phare — pipeline multiphysique de bout en bout

```
γ (design) → Stokes-Brinkman → (u,p) → CHT → T → thermo-élastique → U → J
             filtre 3D + projection Heaviside (β:1→16) ← MMA ← ∇J (TripleAdjoint validé)
```

Démonstrateur `cooling_jacket` (grille 12×12×20, charge thermique piquée au col) :
| Métrique | Valeur |
|---|---|
| Objectif J = FmechᵀU | 6.78 → **1.945** (−71%, décroissance monotone, convergé) |
| Contrainte volume fluide | 0.3995 (cible 0.40) — active, satisfaite à 0.15% |
| Binarisation (Heaviside) | gris 1.0 → **0.056** (design quasi-binaire) |
| **Concentration au col** | densité fluide col 0.577 / extrémités 0.267 = **2.16×** (plus de coolant au point chaud) |
| Temps | 327 s (60 itérations, CPU) |

Sortie : `output/cooling_jacket.vti` (densité + vitesse + température + déplacement,
ParaView). L'optimiseur route le coolant vers le col chaud — le comportement physique
attendu, obtenu par le gradient adjoint validé.

## 4. Choix de conception et raisons

### 4.1 Q1-Q1 + PSPG (ADR-017)
Éléments égal-ordre trilinéaires cohérents avec l'ADN grille structurée/matrix-free ;
Q1-Q1 viole inf-sup → stabilisation PSPG (Brezzi-Pitkäranta). Alternative Taylor-Hood
écartée (nœuds milieux cassent la grille structurée). Couche limite pression O(h)
du PSPG égal-ordre : propriété connue, pression validée par convergence.

### 4.2 Brinkman Borrvall-Petersson, α_max=1e4 (ADR-019)
Frontière fluide-solide = variable de design via α(γ)u. Sweet spot α_max=1e4
(première valeur avec fuite <1%), dans [1e3,1e5] (LL-LIT-004).

### 4.3 CHT + SUPG (ADR-020)
Advection stabilisée SUPG (piège Péclet démontré). Système non-symétrique, direct.

### 4.4 Adjoint triple par cascade inverse (ADR-021)
λ_e → λ_t (via Gᵀλ_e, advection transposée) → λ_s (via (∂R_t/∂u)ᵀλ_t). Validé DF
2.1e-7. **Discipline maintenue** : dérivation exacte, oracle CPU double, DF indépendante,
LL-009 (jugement sur l'accord absolu pour les termes de couplage petits mais réels).

### 4.5 Objectif du démonstrateur = J=FmechᵀU (gradient validé)
Choix assumé : utiliser l'objectif dont le gradient est **rigoureusement validé**,
plutôt qu'un objectif thermique/ΔP non encore validé. Le pipeline complet est ainsi
démontré sans reposer sur un gradient non vérifié.

## 5. Pièges rencontrés (→ LESSONS_LEARNED)

- **LL-010** (nouveau) : Makefile sans dépendances headers → `.o` stale → crash ABI.
  Clean build après toute modif de header.
- **LL-009** appliqué : les termes de couplage fluide/thermique (petits devant
  l'élastique) validés par l'accord absolu à 7e-9, pas l'erreur relative.
- Setup dégénéré du démonstrateur (charge transverse → J non borné) diagnostiqué et
  corrigé (charge alignée sur l'expansion thermique) — rapporté sans maquillage.

## 6. Validations (récap chiffré)

| Test | Cible | Obtenu |
|---|---|---|
| Poiseuille (force + pression) | u < 1% | O(h²), p linéaire |
| inf-sup (PSPG décisif) | pas de damier | α=1e-7 → 4e4× plus bruité |
| Darcy-Brinkman 1D (cosh) | < 2% | 1.2e-3 O(h²) |
| Non-fuite Brinkman | < 1% | α_max=1e4 → 0.47% |
| CHT conduction / adv-diff | exact / O(h²) | 4.4e-15 / ratio 3.76 |
| Piège Péclet SUPG | oscille→propre | démontré |
| **Adjoint triple DF** | **< 1e-3** | **2.1e-7** |
| Démonstrateur (convergence, col) | sensé | J −71%, ratio 2.16× |

## 7. Dette technique / différé (vers l'outil complet)

| Item | Raison | Pour la suite |
|---|---|---|
| **Contraintes cooling jacket spécifiques** (T_max, ΔP, von Mises séparées) | démonstrateur = objectif J=FmechᵀU validé | chaque contrainte = un adjoint-objectif à valider par DF (thermique/ΔP/stress), même machinerie TripleAdjoint avec RHS différent |
| **Vraie géométrie de tuyère** (alésage profilé + col réel) | domaine cartésien pour l'instant | maillage body-fitted ou domaine étendu que l'optimiseur creuse |
| **Adjoint SUPG** | gate à Péclet modéré sans SUPG | différentier la stabilisation SUPG, re-valider DF (haut Péclet) |
| **Portage GPU** (solveurs + adjoints matrix-free float32) | oracles en CPU double | MINRES/Uzawa saddle-point, re-valider contre CPU |
| **Comparaison Borrvall-Petersson quantitative** | démonstrateur qualitatif | reproduire un cas canal publié à ~5% |

## 8. Prompt d'approfondissement (contraintes cooling jacket)

```
Contexte : TopOptP5 a un adjoint triple-couplé Stokes-CHT-élastique VALIDÉ par DF
(2.1e-7) pour J=Fmech^T U, un MMA validé, filtre 3D + Heaviside, et un démonstrateur
end-to-end (cooling_jacket). Pour le VRAI cooling jacket, ajoute les contraintes
physiques, chacune avec son adjoint-objectif validé par DF (<1e-3) AVANT usage :
 1. T_max solide : J_T = p-norm de T sur les éléments solides. Adjoint = back-half
    thermique+Stokes (K_t^T lam_t=-dJ_T/dT, puis A^T lam_s=-(dR_t/du)^T lam_t).
 2. Perte de charge ΔP : J_P = p(inlet)-p(outlet). Adjoint Stokes seul (A^T lam_s=-dJ_P/dw).
 3. von Mises : réutiliser la sensibilité stress p-norm (P4) à travers la cascade.
 Objectif : min masse (ou ΔP) s.t. T_max<=Tlim, von_Mises<=sigma_yield, via MMA
 multi-contraintes. Valide CHAQUE gradient par DF, puis lance l'optim sur une
 géométrie de tuyère (alésage profilé). Vérifie : canaux plus denses au col.
```

## 9. Décisions architecturales
ADR-017..021 (docs/DECISIONS.md) : Q1-Q1 PSPG, oracle CPU, Brinkman, CHT+SUPG,
adjoint triple.

## 10. Checklist de clôture
- [x] Stokes + Brinkman + CHT validés (oracles analytiques quantitatifs)
- [x] **Adjoint triple-couplé validé par DF (2.1e-7)** — le gate le plus dur
- [x] Démonstrateur TO multiphysique end-to-end (convergent, physiquement sensé)
- [x] `make` 0 warning ; `make test_cpu` vert (14 validations)
- [x] `LESSONS_LEARNED.md` : LL-010 ajouté
- [x] `docs/DECISIONS.md` : ADR-017..021
- [x] `handoffs/PHASE_5_TO_6.md` produit
- [x] Différés documentés (contraintes spécifiques, tuyère, SUPG adjoint, GPU)
