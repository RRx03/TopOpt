# Handoff PHASE 5 → PHASE 6

*Rédigé le 2026-07-08 à la clôture de Phase 5. Le code fait foi : `TopOptP5/`,
`TopOptP5/PHASE_5_REPORT.md`.*

---

## État en sortie de Phase 5

**Le cœur scientifique du projet est validé de bout en bout** : TO multiphysique
fluide-structure-thermique par gradient adjoint. `make test_cpu` = 14 validations
vertes, 0 warning.

```
TopOptP5/src/  (ajouts Phase 5)
├── physics/StokesSolver.{hpp,cpp}     # Stokes Q1-Q1 PSPG + Brinkman α(γ)u
├── physics/CHTSolver.{hpp,cpp}        # advection-diffusion (CHT) + SUPG
├── adjoint/TripleAdjoint.{hpp,cpp}    # adjoint triple-couplé (GATE DF 2.1e-7)
└── apps/cooling_jacket.cpp            # démonstrateur TO multiphysique end-to-end
```

### Acquis validés (mesuré)
- Stokes (Poiseuille force+pression), Brinkman (Darcy-Brinkman cosh 1.2e-3, non-fuite),
  CHT (adv-diff O(h²), piège Péclet), **adjoint triple DF 2.1e-7**, démonstrateur
  (J −71%, ratio col 2.16×, quasi-binaire).

### Décisions (ADR-017..021)
Q1-Q1 PSPG ; oracle CPU double ; Brinkman BP α_max=1e4 ; CHT+SUPG ; adjoint triple
par cascade inverse.

### Différé (cf. report §7-8, prompts d'approfondissement fournis)
- Contraintes cooling jacket spécifiques (T_max, ΔP, von Mises) — chacune un
  adjoint-objectif à valider par DF.
- Vraie géométrie de tuyère (alésage profilé) — commun avec le différé P4.
- Adjoint SUPG (haut Péclet) ; portage GPU (solveurs + adjoints) ; comparaison
  quantitative Borrvall-Petersson.

---

## Phase 6 — choix à arbitrer (cf. `prompts/PHASE_6_BRIEF.md`)

Le cœur scientifique étant livré, Phase 6 valorise l'acquis. **Décision à prendre
en début de session 1**, selon l'objectif (recrutement / thèse / entreprise) et le
temps restant. Trois directions :
- **A — Industrialisation** (recrutement) : manufacturing constraints (Langelaar),
  robust TO, validation expérimentale, documentation/gallery.
- **B — Recherche** (thèse) : AMR, adjoint adaptatif, Navier-Stokes/RANS.
- **C — Verticalisation** (entreprise) : moteur complet, pipeline spec→AM→test.

## Recommandation de séquencement (avant tout choix Phase 6)

D'abord **solder les différés qui rendent le démonstrateur convaincant** (cf. VISION §6,
signal recruteur) — ce sont des refinements sans nouveau gate fondamental :
1. **Contraintes cooling jacket réelles** (au moins T_max) + **vraie géométrie de
   tuyère** → un livrable « cooling jacket sur tuyère » défendable industriellement.
2. **Visualisation** : le `VTKExporter`/ParaView est en place ; marching cubes lisse
   pour le rendu gallery.
3. **Comparaison à un cas publié** (Borrvall-Petersson) à ~5% → validation littérature.

Ces trois items transforment le démonstrateur validé en dossier recruteur (le but
de fin de Phase 5 selon VISION.md §6). **Recommandé avant d'attaquer une direction
Phase 6.**

---

## Livrables recruteur — état (soldés 2026-07-08)
- [x] **Validation vs papier publié** : diffuseur Borrvall-Petersson reproduit
- [x] **Contrainte cooling jacket réelle** : adjoint T_max validé DF (7.5e-8)
- [x] **Vraie tuyère à col profilé** : alésage convergent-divergent (Lamé-validé), masse −61%
- [x] **Marching cubes** : STL lisse (aire/volume sphère 0.06%, watertight)
- [x] **Livrable théorique** : `docs/THEORY.md` (méthode + 6 gates + résultats)
- [x] Validation rigoureuse par DF (6 gates adjoints) — **différenciant fort**
- [x] Export VTK/ParaView (densité, contrainte, température, vitesse)
- [x] Limitations honnêtement documentées
- [ ] Benchmark report grande échelle (nécessite portage GPU des adjoints) — différé
- [ ] Cooling jacket sur la vraie tuyère profilée sous contraintes réelles combinées
      (T_max + von Mises + ΔP) — assemblage des briques validées, différé

Le socle recruteur est **substantiellement complet** : validation littérature,
contraintes réelles validées, vraie géométrie, visualisation, document théorique.
Restent des assemblages (pas de nouveau gate fondamental).

## Pièges spécifiques Phase 6 (LESSONS_LEARNED)
Selon la direction (cf. brief) : Langelaar overhang (A), stabilisation NS/RANS (B),
etc. + tous les LL accumulés (LL-001..010, LL-LIT-001..012).
