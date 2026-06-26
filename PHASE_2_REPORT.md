# Rapport — Phase 2 : passage 3D + Metal GPU compute

> ⚠️ **AVERTISSEMENT D'HONNÊTETÉ** — Ce rapport documente l'état réel : seule
> la **session 1 (fondation Metal)** est faite. Le cœur de Phase 2 (FEM 3D,
> CG GPU, assembly CSR, SpMV, STL, benchmark 128³) **n'est PAS réalisé**.
> Phase 2 n'est donc **PAS substantiellement clôturée** au sens des checkpoints
> de `TRANSITIONS.md`. Ce fichier sert de **checkpoint de session 1** et débloque
> l'outillage, mais la "règle d'or" (ne pas avancer si checkpoints non verts)
> implique de **terminer Phase 2 avant de démarrer réellement Phase 3**.

*Rapport rédigé le 2026-06-15.*

**Date** : 2026-06-15 (checkpoint session 1, pas une clôture)
**Durée réelle** : 1 session sur 6-8 semaines cible
**Dernier commit** : WIP — non commité

---

## 1. État du code

### Arborescence finale (session 1)
```
TopOptP2/
├── Makefile, CLAUDE.md, STATUS.md, TASKS.md, .gitignore
├── src/gpu/
│   ├── MetalContext.{hpp,cpp}   # device, queue, library, capacités
│   └── metal_impl.cpp           # TU unique *_PRIVATE_IMPLEMENTATION
├── shaders/vector_add.metal     # kernel vec_add (démo)
├── tests/test_metal_hello.cpp   # vec add 1M GPU vs CPU
├── docs/{ARCHITECTURE,SYMBOLS,DECISIONS}.md
└── third_party/metal-cpp/       # vendorisé (macOS26/iOS26)
```

### Conventions de code
- Standard : C++23 ✓ — Build : Makefile two-phase ✓
- metal-cpp en `-isystem`, `-fno-objc-arc`, `*_PRIVATE_IMPLEMENTATION` TU unique ✓
- Floating point : float (démo GPU) ✓ — choix float/double FEM différé
- MASTER_CLAUDE.md respecté : Oui

---

## 2. Acquis validés (vs checkpoints Phase 2 de TRANSITIONS.md)

| Acquis attendu Phase 2 | Statut | Notes |
|---|:---:|---|
| Fondation Metal (device/queue/library/pipeline) | ✓ | MetalContext + hello-world |
| Tout l'acquis Phase 1 porté en 3D | ✗ | non commencé |
| Solveur CG préconditionné Jacobi GPU | ✗ | non commencé |
| Assembly K format CSR sur GPU | ✗ | non commencé |
| SpMV creux sur GPU | ✗ | non commencé |
| Loop CG complète GPU | ✗ | non commencé |
| Marching cubes ρ=0.5 → STL | ✗ | non commencé |
| Test poutre console 3D | ✗ | non commencé |
| Benchmark 128³ < 10 min | ✗ | non commencé |

**Bilan : 1/9 — fondation seule.**

---

## 3. Cas tests validés

| Test | Métrique | Cible | Obtenu | Statut |
|---|---|---|---|:---:|
| Hello-world vec add GPU | max\|gpu-cpu\| (1M floats) | < 1e-6 | 0.000e+00 | ✓ |
| Init device Metal | nom/family/mémoire | non-null | M4 Max / Apple9 / 55.66 GB | ✓ |
| Test patch FEM 3D | erreur L2 | 1e-6 (float) | — | ✗ à faire |

---

## 4. Benchmarks de performance

| Cas | Détail | Obtenu | Cible |
|---|---|---|---|
| vec_add | 1 048 576 floats, StorageModeShared | exact, < 1 ms | démo |
| 128³ solve | — | non mesuré | < 10 min (Phase 2 complète) |

Hardware : Mac Studio M4 Max, 64 GB unified, GPU family Apple9, ~55.7 GB working set.

---

## 5. Dette technique acceptée

| Item | Raison | Résolution |
|---|---|---|
| 2D non encore porté en 3D | session 1 = fondation GPU isolée | sessions Phase 2 suivantes |
| Précision float/double FEM non tranchée | dépend des kernels réels | avant l'assembly 3D |
| Pas de framework graphique | compute pur demandé | Phase 2+ (STL offline) |

---

## 6. Modifications requises pour la suite de Phase 2 (PAS Phase 3)

| Composant | État | Prochaine étape |
|---|---|---|
| Élément | aucun | H8 hexaédrique + KE0 3D |
| Assembly | aucun | CSR + kernel Metal (atomicAdd, LL-LIT-008) |
| Solveur | aucun | CG Jacobi GPU (LL-LIT-009 sur précision float) |
| Densité/SIMP/OC/filtre | en P1 (2D) | porter en 3D |

---

## 7. Pièges rencontrés et solutions

### Piège 1 : fichier handoff référencé inexistant
- **Symptôme** : prompts référençaient `PHASE_1_TO_PHASE_2_HANDOFF.md`, absent.
- **Solution** : utilisé `TopOptP1/TRANSITIONS.md` (la vraie cartographie).
- **LESSONS** : candidate (cf. §8).

### Piège 2 : `-fno-objc-arc` sur TU C++ pur
- **Symptôme** : crainte de warning "argument unused" sur les .cpp non-ObjC.
- **Solution** : vérifié — clang ne warn pas ; `-fno-objc-arc` accepté sans bruit. Build 0 warning.

### Piège 3 : `newDefaultLibrary()` en CLI
- **Symptôme** : ne fonctionne qu'en .app bundle.
- **Solution** : `newLibrary("build/shaders.metallib")` pour exécutable CLI. Déjà dans MASTER_CLAUDE.

---

## 8. Mise à jour LESSONS_LEARNED.md proposée

```markdown
### LL-004 : Références de fichiers de handoff inexistantes (Phase 2, 2026-06-15)
- Symptôme : prompt référence un doc (PHASE_1_TO_PHASE_2_HANDOFF.md) qui n'existe pas.
- Cause : nom de fichier supposé, jamais créé.
- Conséquence : risque de bloquer ou de partir sur de mauvaises hypothèses.
- Leçon : vérifier l'existence des fichiers de référence AVANT de s'appuyer dessus ; si absent, identifier le vrai doc (ici TRANSITIONS.md) et le signaler.
- Vérification : `ls` des fichiers de référence en début de session.
```

---

## 9. Écarts par rapport au plan initial

| Élément | Plan | Réalisé | Justification |
|---|---|---|---|
| Démarrage TopOptP2 | "copier structure P(N-1)" | vierge + Metal seul | choix utilisateur (séquençage, foundation isolée) |
| Vendoring metal-cpp | download Apple | copie locale officielle macOS26 | déjà sur disque, version alignée SDK 26.2 |
| Périmètre Phase 2 | phase complète | session 1 (fondation) | une marche à la fois (demandé) |

---

## 10. Limitations documentées

- Aucun solveur 3D ni TO 3D encore — uniquement l'infrastructure GPU.
- float en démo ; précision FEM non décidée.
- Pas de visualisation.
- **Phase 2 non terminée** : ne pas démarrer Phase 3 sur cette base sans finir le solveur 3D GPU.

---

## 11. Décisions architecturales (cf. TopOptP2/docs/DECISIONS.md)

```markdown
## ADR-001 : TopOptP2 vierge (Metal-only), non copié de P1
## ADR-002 : Réutiliser le metal-cpp officiel local (macOS26)
## ADR-003 : Build 0 warning — -isystem + -fno-objc-arc + TU impl unique
## ADR-004 : float GPU pour la démo, choix FEM différé
```
(Détail complet dans `docs/DECISIONS.md`.)

---

## 12. Checklist de clôture de phase

- [x] `PHASE_2_REPORT.md` rédigé (checkpoint session 1)
- [ ] `TRANSITIONS.md` section Phase 2 : acquis 3D **non** validés (1/9)
- [x] `LESSONS_LEARNED.md` : LL-004 proposé
- [x] `docs/DECISIONS.md` à jour (ADR-001..004)
- [x] `make` 0 warning, `make test` passe (hello-world)
- [ ] Git : non commité (WIP)
- [ ] **Phase 2 substantiellement complète** : NON — solveur 3D GPU à construire
