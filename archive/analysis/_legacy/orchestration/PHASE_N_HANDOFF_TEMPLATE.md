# PHASE_N_REPORT.md — Template de rapport de fin de phase

*Copier ce fichier sous `TopOptPN/PHASE_N_REPORT.md` à la fin de chaque phase.*
*Remplir toutes les sections avant de clôturer la phase.*
*Aucun champ optionnel : si la section ne s'applique pas, écrire "N/A — justification".*

---

# Rapport de fin — Phase N : [Titre de la phase]

**Date de clôture** : YYYY-MM-DD
**Durée réelle** : X semaines (cible : Y semaines)
**Dernier commit** : `<hash>` — `<message>`

---

## 1. État du code

### Arborescence finale
```
TopOptPN/
├── src/
│   ├── ...   (liste les dossiers/fichiers effectivement créés)
├── shaders/
│   └── ...
├── tests/
│   └── ...
└── ...
```

### Conventions de code (confirmées ou modifiées)
- Standard : C++23 ✓ / modifié : [préciser]
- Build : Makefile ✓ / modifié : [préciser]
- Naming : PascalCase/camelCase/UPPER_SNAKE ✓ / modifié : [préciser]
- Floating point : double CPU / float GPU ✓ / modifié : [préciser]
- Tout le reste de MASTER_CLAUDE.md respecté : Oui / Non [lister les écarts]

---

## 2. Acquis validés

*Reprendre la liste de checkpoints du PHASE_N_INITIAL_PROMPT.md. Cocher ou expliquer.*

| Acquis | Statut | Notes |
|---|:---:|---|
| [Acquis 1 du prompt] | ✓ | |
| [Acquis 2 du prompt] | ✓ | |
| [Acquis 3 du prompt] | ⚠️ | Partiellement — [expliquer l'écart] |
| [Acquis 4 du prompt] | ✗ | Reporté en Phase N+1 — [justification] |

---

## 3. Cas tests validés

| Test | Résultat | Valeur cible | Valeur obtenue | Statut |
|---|---|---|---|:---:|
| Test patch FEM | erreur L2 | < 1e-10 (double) | [valeur] | ✓/✗ |
| Cas analytique | erreur relative | < 1% | [valeur] | ✓/✗ |
| Adjoint DF (Phase 4+) | max|adjoint-DF|/|DF| | < 1e-5 | [valeur] | ✓/✗ |
| Cas canonique littérature | [métrique] | [cible] | [valeur] | ✓/✗ |
| Mesh independence (Phase 3+) | design convergé | visuellement identique | [oui/non] | ✓/✗ |

---

## 4. Benchmarks de performance

| Cas test | Résolution | Temps/itération TO | Temps total | Mémoire pic | Cible |
|---|---|---|---|---|---|
| [Cas 1] | [Nx³] | [s] | [min] | [GB] | [cible Phase N] |
| [Cas 2] | [Nx³] | [s] | [min] | [GB] | |

Hardware : Mac Studio M[X], [N] GB unified memory.

---

## 5. Dette technique acceptée

*Tout ce qui a été intentionnellement laissé imparfait, avec justification.*

| Item | Raison | Phase prévue pour résolution |
|---|---|---|
| [Item 1] | [Justification] | Phase N+1 |
| [Item 2] | [Justification] | Phase N+2 |

---

## 6. Modifications requises pour Phase N+1

*Ce que le code suivant DOIT changer ou ajouter pour démarrer correctement.*

| Composant | État Phase N | Modification Phase N+1 |
|---|---|---|
| [Composant 1] | [état actuel] | [ce qui change] |
| [Composant 2] | [état actuel] | [ce qui change] |

---

## 7. Pièges rencontrés et solutions trouvées

*Tout piège non documenté dans LESSONS_LEARNED.md avant cette phase.*

### Piège 1 : [Titre]
- **Symptôme** : [ce qui s'est passé]
- **Cause** : [pourquoi]
- **Solution** : [comment résolu]
- **À ajouter à LESSONS_LEARNED.md** : Oui (LL-XXX) / Non (déjà connu)

*(Répéter pour chaque piège)*

---

## 8. Mise à jour LESSONS_LEARNED.md proposée

*Nouvelles entrées à ajouter, format complet prêt à copier-coller.*

```markdown
### LL-XXX : [Titre] (Phase N, YYYY-MM-DD)
- **Symptôme** : ...
- **Cause** : ...
- **Conséquence** : ...
- **Leçon** : ...
- **Vérification** : ...
```

*(Une entrée par nouveau piège ou par piège connu qui a quand même mordu)*

---

## 9. Écarts par rapport au plan initial

*Tout ce qui a changé par rapport au PHASE_N_INITIAL_PROMPT.md, avec justification.*

| Élément | Plan initial | Réalisé | Justification |
|---|---|---|---|
| [Élément 1] | [plan] | [réalisé] | [pourquoi] |

---

## 10. Limitations documentées (fin de phase)

*Ce que ce solveur ne fait PAS, exprimé pour le recruteur et le lecteur.*

- [Limitation 1] — sera adressée en Phase N+1 / jamais / optionnel
- [Limitation 2] — ...

---

## 11. Décisions architecturales prises (à ajouter dans DECISIONS.md)

```markdown
## ADR-NNN : [Titre]
- **Date** : YYYY-MM-DD
- **Contexte** : [Pourquoi cette décision se pose]
- **Options considérées** : [Liste]
- **Décision** : [Ce qu'on a choisi]
- **Conséquences** : [Ce que ça implique pour la suite]
```

---

## 12. Checklist de clôture de phase

*Toutes les cases doivent être cochées avant de passer à la phase suivante.*

- [ ] `PHASE_N_REPORT.md` complet et relu
- [ ] `TopOptP1/TRANSITIONS.md` section Phase N mise à jour (acquis validés)
- [ ] `orchestration/LESSONS_LEARNED.md` mis à jour avec nouvelles entrées
- [ ] `docs/DECISIONS.md` mis à jour avec les ADR de cette phase
- [ ] `docs/SYMBOLS.md` mis à jour avec les nouveaux symboles publics
- [ ] `STATUS.md` reflète l'état réel du code (pas "en cours" si terminé)
- [ ] `TASKS.md` : tâches cochées avec dates, nouvelles tâches Phase N+1 listées
- [ ] Aucun test cassé dans `make test`
- [ ] Aucun warning de compilation (`make -B 2>&1 | grep -i warning | wc -l` = 0)
- [ ] Git : tous les commits de la phase ont des messages Conventional Commits
- [ ] Git : aucun fichier de build commité (`.gitignore` à jour)
- [ ] `orchestration/tools/close_phase.sh N` lancé sans erreur
