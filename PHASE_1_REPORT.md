# Rapport de fin — Phase 1 : TO structurelle 2D, SIMP, adjoint analytique

*Rapport rédigé rétroactivement le 2026-06-15 à partir de la session Phase 1.*

**Date de clôture** : 2026-06-15 (rétroactif)
**Durée réelle** : 1 session (cible : 4-6 semaines)
**Dernier commit** : WIP — non commité

---

## 1. État du code

### Arborescence finale
```
TopOptP1/
├── Makefile, README.md, CLAUDE.md, TRANSITIONS.md, .gitignore
├── src/
│   ├── main.cpp                  # CLI "mbb", boucle TO, I/O PNG
│   ├── core/Grid2D.{hpp,cpp}     # indexation noeuds/elements/DOF (column-major top88)
│   ├── fem/FEM2D.{hpp,cpp}       # KE0 Q4, assemblage reduit, Dirichlet, solve LDLT
│   ├── topopt/SIMP.{hpp,cpp}     # interpolation, sensibilites, OC
│   ├── filter/Helmholtz.{hpp,cpp}# filtre PDE densite
│   └── io/PNGWriter.{hpp,cpp}    # export niveaux de gris (stb)
├── third_party/{eigen,nlohmann,stb}/
├── tests/test_mbb_beam.cpp
└── assets/problem_mbb.json
```

### Conventions de code
- Standard : C++23 ✓
- Build : Makefile ✓ (deps tierces en `-isystem` pour 0 warning)
- Naming : PascalCase/camelCase/UPPER_SNAKE ✓
- Floating point : `double` partout (CPU) ✓
- MASTER_CLAUDE.md respecté : Oui (écart assumé : solveur direct, cf. §9)

---

## 2. Acquis validés

| Acquis | Statut | Notes |
|---|:---:|---|
| Assembly FEM 2D Q4 bilinéaire | ✓ | KE0 canonique top88 |
| Solveur direct K U = F | ✓ | `Eigen::SimplicialLDLT` |
| Loi SIMP E(ρ) | ✓ | Emin=1e-9, p=3 |
| Gradient adjoint analytique compliance | ✓ | dc = -p ρ^(p-1)(E0-Emin) uᵀKE0u |
| Filtre Helmholtz | ✓ | r = R/(2√3), R=2 cellules |
| Update OC + move-limit | ✓ | move=0.2, bissection sur λ |
| Visualisation PNG | ✓ | iter_*.png + final.png |
| Cas MBB design canonique | ✓ | treillis symétrique correct |

---

## 3. Cas tests validés

| Test | Métrique | Cible | Obtenu | Statut |
|---|---|---|---|:---:|
| Patch / barre traction | max\|u-analytique\| | < 1e-10 (double) | 2.8e-14 | ✓ |
| Filtre champ uniforme | max\|H(x)-x\| | ~0 | 3.3e-16 | ✓ |
| MBB court (60×20, 25 it) | compliance ↓ | décroissante | 1007 → 242 | ✓ |
| Volume final (200×100) | \|vol-0.5\| | < 1% | 0.0000 | ✓ |
| Reproduction Andreassen 60×20 | compliance | ~200 ±qqs % | 229.9 (converge change<1e-2) | ✓ |

---

## 4. Benchmarks de performance

| Cas | Résolution | Temps total | Volume | Compliance | Cible |
|---|---|---|---|---|---|
| MBB | 200×100 (20k elem) | ~16 s / 100 it | 0.5000 | 84.4 | < 30 s ✓ |
| MBB | 60×20 | 0.43 s / 116 it | 0.5000 | 229.9 | — |

Hardware : Mac Studio M4 Max, 64 GB unified memory.

---

## 5. Dette technique acceptée

| Item | Raison | Résolution prévue |
|---|---|---|
| Visualisation PNG only | suffisant en 2D | Phase 2 (STL/marching cubes) |
| Pas de CI automatisée | validation manuelle OK | ultérieur |
| Mono-thread, Eigen dense par endroits | petits cas 2D | Phase 2 (GPU) |
| Solveur direct (pas CG) | conditionnement Emin=1e-9 | Phase 2 (CG sur GPU) |
| Pas de continuation de p (p=3 direct) | converge proprement à cette taille | si oscillations en 3D |

---

## 6. Modifications requises pour Phase 2

| Composant | État Phase 1 | Modification Phase 2 |
|---|---|---|
| Dimension | 2D Q4 | 3D H8 |
| Solveur | Direct LDLT (CPU) | CG préconditionné Jacobi (GPU) |
| Backend | Eigen CPU | Metal compute |
| Stockage K | `Eigen::SparseMatrix` | CSR custom + `MTLBuffer` |
| Assembly | loop CPU | kernel Metal (atomicAdd, cf. LL-LIT-008) |
| Filtre | r en cellules | r en mm en Phase 3 (cf. LL-LIT-006) |

---

## 7. Pièges rencontrés et solutions

### Piège 1 : "MBB beam" décrit avec BCs cantilever
- **Symptôme** : prompt décrivait "MBB" mais BCs = encastrement gauche + charge haut-droite.
- **Cause** : confusion sémantique de la spec.
- **Solution** : clarification explicite (MBB classique : symétrie gauche, roller bas-droite, charge haut-gauche).
- **LESSONS** : déjà LL-001.

### Piège 2 : compliance de référence ≈200 attribuée à 200×100
- **Symptôme** : spec attendait compliance ≈200 à 200×100 ; obtenu 84.
- **Cause** : ~200 est le chiffre 60×20 d'Andreassen ; la compliance scale en ~L³/H³, non invariante au maillage (élément = taille unité).
- **Solution** : vérification à 60×20 → 229.9 (cohérent). Documenté.
- **LESSONS** : candidate LL (cf. §8).

### Piège 3 : warnings Eigen sous C++23 et collision nom binaire
- **Symptôme** : `-Wdeprecated` dans Eigen ; `build/topopt` (binaire) vs `build/topopt/` (objets module).
- **Solution** : deps en `-isystem` ; objets sous `build/obj/`.
- **LESSONS** : candidate LL (build).

---

## 8. Mise à jour LESSONS_LEARNED.md proposée

> **Statut (2026-06-26)** : intégrées dans `orchestration/LESSONS_LEARNED.md`,
> **renumérotées LL-004 et LL-005** (les numéros LL-002/LL-003 étaient déjà pris
> par d'autres entrées au moment du reset documentaire).

```markdown
### LL-004 : Compliance non invariante au maillage (Phase 1, 2026-06-15)
- Symptôme : valeur de compliance de référence d'un papier appliquée à une autre résolution.
- Cause : domaine défini en unités-élément → grandit avec le maillage ; c ~ L³/H³.
- Conséquence : faux "échec" de validation, ou faux succès.
- Leçon : une compliance de référence n'est valable QU'À la résolution/géométrie du papier.
- Vérification : reproduire le cas canonique sur SA grille avant de comparer.

### LL-005 : Build clang — deps tierces et collisions de noms (Phase 1, 2026-06-15)
- Symptôme : warnings Eigen sous -Wpedantic ; binaire et dossier d'objets homonymes.
- Cause : -I au lieu de -isystem ; binaire build/X et objets build/X/.
- Conséquence : "0 warning" impossible ; erreur de link EISDIR.
- Leçon : deps en -isystem ; objets sous build/obj/, binaires ailleurs.
- Vérification : make clean && make 2>&1 | grep -c warning == 0.
```

---

## 9. Écarts par rapport au plan initial

| Élément | Plan initial | Réalisé | Justification |
|---|---|---|---|
| Solveur linéaire | CG (suggéré CLAUDE.md) | SimplicialLDLT direct | Emin=1e-9 → mauvais conditionnement ; direct robuste et < 30 s (validé avec l'utilisateur) |
| Rayon filtre | "2 cellules" | r = R/(2√3), R=2 | mapping standard Lazarov-Sigmund (validé) |

---

## 10. Limitations documentées

- 2D uniquement — Phase 2 passe en 3D.
- Solveur direct ne scalera pas au-delà de ~10⁵ DOF — Phase 2 (CG GPU).
- Pas de contraintes mécaniques (von Mises) — Phase 4.
- Filtre en cellules (mesh-dependent) — Phase 3 (r en mm).

---

## 11. Décisions architecturales (cf. docs/DECISIONS.md de P1)

```markdown
## ADR-001 : Solveur direct SimplicialLDLT (Phase 1)
- Date : 2026-06-04
- Contexte : K mal conditionnée (Emin=1e-9), CG suggéré mais risqué.
- Décision : SimplicialLDLT direct.
- Conséquences : robuste et rapide en 2D ; CG sur GPU dès Phase 2.

## ADR-002 : Filtre Helmholtz r = R/(2√3)
- Date : 2026-06-04
- Décision : mapping standard Lazarov-Sigmund, R=2 cellules.
- Conséquences : pas de damier ; à passer en mm en Phase 3.
```

---

## 12. Checklist de clôture de phase

- [x] `PHASE_1_REPORT.md` complet et relu
- [x] `TopOptP1/TRANSITIONS.md` section Phase 1 (acquis ✓ documentés)
- [x] `orchestration/LESSONS_LEARNED.md` — LL-001 présent ; LL-002/003 proposés ci-dessus
- [x] `docs/` — N/A pour P1 (scaffold doc non créé en Phase 1)
- [x] `make` 0 warning, `make test` passe
- [ ] Git : phase non commitée (WIP) — à committer si souhaité
- [x] Aucun build commité (`.gitignore` à jour)
