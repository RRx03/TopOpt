# Handoff PHASE 4 → PHASE 5

*Rédigé le 2026-06-29 à la clôture de Phase 4. Le code fait foi : `TopOptP4/`,
`TopOptP4/PHASE_4_REPORT.md`.*

---

## État du code en sortie de Phase 4

**Phase 4 substantiellement complète** : thermo-élastique + stress + axisym, avec
5 gates DF/oracles. `make` 0 warning, `make test_cpu` vert.

```
TopOptP4/src/  (ajouts Phase 4)
├── physics/ThermalSolver.{hpp,cpp}        # conduction GPU matrix-free −div(k∇T)=q
├── physics/ThermoElasticCoupling.hpp      # F_th = E·α·Cth·(T−Tref)
├── topopt/StressModel.hpp                 # von Mises 3D + qp-relax + p-norm
├── topopt/StressModelAxi.hpp              # von Mises 4-comp axisym + p-norm
├── topopt/MMAOptimizer.{hpp,cpp}          # MMA Svanberg 1987 (dual)
├── adjoint/ThermoElasticAdjoint.{hpp,cpp} # adjoint 2 blocs (compliance + stress)
├── adjoint/AxiStressAdjoint.{hpp,cpp}     # adjoint stress axisym (élastique)
├── core/Grid2DAxi.hpp, fem/AxiQ4Element.*, fem/FEM2DAxi.*   # FEM axisym
└── apps/nozzle_axi.cpp                     # démo tuyère axisym structurelle
```

### Acquis validés (mesuré)
- Adjoint compliance DF 1.6e-6 ; adjoint stress p-norm DF 1.6e-7 ; adjoint stress
  axisym DF 2.7e-9 ; MMA optimum analytique 6.4e-14 (OC 0.037 %) ; Lamé ordre 2.
- Démonstrateur tuyère axisym : masse −53 %, contrainte active, **épaississement
  au col ratio 3.11×**.

### Décisions (ADR-013..016, cf. report §9)
- qp-relaxation + p-norm ; adjoint unifié compliance/stress ; MMA dual ;
  **seam 3D/axisym** (deux pistes FEM, à unifier).

### Différé (assumé, vers l'outil complet 3D+thermique)
- **Vraie tuyère** (alésage profilé, l'actuel est à alésage constant) ; thermique en
  axisym ; démo 3D thermo-élastique complète (stack 3D validée, zéro
  gate manquant) ; portage GPU des adjoints (float32, à re-valider). Prompts
  d'approfondissement dans `PHASE_4_REPORT.md` §8.

---

## Prérequis Phase 5 — état

Phase 5 = Stokes + CHT (fluide), adjoint **triple-couplé**. La fondation de Phase 4
fournit :
- [x] Adjoint multi-bloc **validé par DF** (la méthode à généraliser au 3ᵉ bloc fluide)
- [x] MMA multi-contraintes opérationnel
- [x] Stress p-norm + relaxation ; filtre (Helmholtz mm en P3)
- [~] Solveurs : élastique + thermique. **À ajouter en P5** : Stokes (saddle-point).
- [~] Le triple-couplage suppose idéalement la thermique unifiée sur la géométrie
  cible — voir le différé thermique-axi / démo 3D.

> **Recommandation** : avant Phase 5, solder au moins une cible du différé §7
> (idéalement la **démo 3D thermo-élastique complète**, sans nouveau gate) pour
> disposer d'un pipeline thermo-élastique-stress de bout en bout sur lequel
> greffer le bloc fluide.

---

## Modifications requises pour Phase 5

| Composant | Phase 4 | Phase 5 |
|---|---|---|
| Physique | élasticité + thermique | + **Stokes incompressible** (Brinkman) |
| Solveur linéaire | CG/LDLT (SPD) | + **saddle-point** (MINRES/Uzawa, indéfini) |
| Couplage | thermo-élastique (2 blocs) | + CHT → **adjoint 3 blocs** |
| Éléments | H8 / Q4 axisym | + **Taylor-Hood** ou Q1-Q1 PSPG (inf-sup) |
| Validation | DF 2 blocs (1e-5) | **DF 3 blocs (1e-3)** — gate le plus dur du projet |
| Filtre | Helmholtz (mm) | + **projection Heaviside** (binarisation) |

---

## Pièges spécifiques Phase 5 (LESSONS_LEARNED)

- **LL-LIT-002** inf-sup : décider Taylor-Hood vs Q1-Q1 PSPG avant de coder Stokes.
- **LL-LIT-012** non-dimensionnalisation : échelles avant assemblage (cond < 1e12).
- **LL-LIT-004** Brinkman α_max ∈ [1e3,1e5], continuation.
- **LL-LIT-007** adjoint triple : **DF obligatoire avant tout run**, sinon STOP.
- **LL-009** (validation DF) : juger sur l'accord absolu ; sweep ε.

---

## Validations obligatoires fin de Phase 5
- Poiseuille (Stokes) < 0.5 % ; inf-sup (pas d'oscillation pression) ;
  CHT analytique < 1 % ; **adjoint 3 blocs DF < 1e-3** ; Borrvall-Petersson ~5 % ;
  cooling jacket : canaux concentrés au col.

## Référence canonique
Borrvall-Petersson 2003 ; Dilgen et al. 2018 ; Alexandersen-Andreasen 2020 ;
Wang-Lazarov-Sigmund 2011 (Heaviside).
