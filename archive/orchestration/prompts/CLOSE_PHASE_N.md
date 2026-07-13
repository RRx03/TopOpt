# CLOSE_PHASE_N.md — Template "un prompt suffit" (clôture de phase)

## INSTRUCTIONS POUR L'UTILISATEUR

Pour clôturer la phase N :
1. Copier le bloc sous « PROMPT À DONNER À CLAUDE CODE ».
2. Remplacer chaque `{N}` par le numéro réel et `{N+1}` par N+1.
3. Coller dans la session Claude Code de la phase (ou une session fraîche à la racine).

---

# PROMPT À DONNER À CLAUDE CODE

La **Phase {N}** touche à sa fin. Tu vas la clôturer proprement et honnêtement.

## LECTURES PRÉALABLES
1. `orchestration/MASTER_CLAUDE.md` — protocole de clôture
2. `orchestration/prompts/PHASE_{N}_BRIEF.md` — checkpoints et validations attendus
3. `TopOptP{N}/` — code, tests, docs réels

## ACTIONS À EXÉCUTER

### Étape A — Validation finale (factuelle, pas optimiste)
- Lancer `make && make test` ; **coller les résultats réels** (valeurs numériques).
- Vérifier chaque cas test prévu dans `PHASE_{N}_BRIEF.md` (section Validation).
- Vérifier que les benchmarks de performance sont dans les cibles (mesurés).
- **Lister explicitement les écarts** (checkpoints non atteints). Honnêteté absolue :
  si la phase n'est pas complète, le dire (cf. exemple `TopOptP2/PHASE_2_REPORT.md`).

### Étape B — `TopOptP{N}/PHASE_{N}_REPORT.md`
Document factuel (sections obligatoires) :
1. **État du code** : arborescence réelle, conventions confirmées/modifiées.
2. **Acquis validés** : tableau checkpoint par checkpoint (✓ / ⚠️ / ✗ + note).
3. **Cas tests** : test, métrique, cible, valeur obtenue, statut.
4. **Benchmarks** : cas, résolution, temps/itération, total, mémoire pic, cible.
5. **Dette technique acceptée** : item, raison, phase de résolution prévue.
6. **Modifications requises pour Phase {N+1}** : composant, état actuel, changement.
7. **Pièges rencontrés et solutions** (réfs commits Git).
8. **Mise à jour LESSONS_LEARNED proposée** (entrées prêtes à copier).
9. **Écarts vs plan initial** (avec justification).
10. **Limitations documentées** (ce que le solveur ne fait pas).
11. **ADR** de la phase (pour `docs/DECISIONS.md`).
12. **Checklist de clôture** (cases à cocher, cf. ci-dessous).

### Étape C — `orchestration/handoffs/PHASE_{N}_TO_{N+1}.md`
- État du code en sortie (issu du rapport, factuel).
- Modifications requises pour Phase {N+1} (tableau Phase {N} → Phase {N+1}).
- Architecture cible Phase {N+1} (arborescence prévue).
- Pièges spécifiques anticipés (renvois LESSONS_LEARNED).
- Validations obligatoires de fin de Phase {N+1} (tolérances).

### Étape D — `orchestration/LESSONS_LEARNED.md`
Ajouter les pièges rencontrés en Phase {N}, format standardisé
(`LL-XXX : Titre (Phase {N}, AAAA-MM-JJ)` + Symptôme/Cause/Conséquence/Leçon/Vérification).
Si aucun nouveau piège : l'écrire explicitement.

### Étape E — Mises à jour transverses
- `TopOptP1/TRANSITIONS.md` : cocher les acquis Phase {N} réellement validés.
- `TopOptP{N}/docs/SYMBOLS.md` : nouveaux symboles publics.
- `TopOptP{N}/STATUS.md` et `TASKS.md` : refléter l'état réel.

### Étape F — Commit Git final
Proposer (sans exécuter sans accord) :
`git commit -m "docs(phase{N}): close phase {N} — report, handoff, lessons"`

## CHECKLIST DE CLÔTURE (toutes cochées avant de passer à Phase {N+1})
- [ ] `PHASE_{N}_REPORT.md` complet et relu
- [ ] `handoffs/PHASE_{N}_TO_{N+1}.md` produit
- [ ] `LESSONS_LEARNED.md` mis à jour (ou mention "aucune entrée")
- [ ] `TRANSITIONS.md` : acquis Phase {N} cochés
- [ ] `docs/DECISIONS.md` : ADR de la phase ajoutés
- [ ] `docs/SYMBOLS.md` : symboles publics à jour
- [ ] `make test` vert ; `make` sans warning
- [ ] Aucun fichier de build commité (`.gitignore` à jour)
- [ ] Commit de clôture proposé à l'utilisateur

## CONTRAINTE
**Honnêteté absolue** sur les écarts : aucun polish marketing. Référencer les
commits Git pour la traçabilité. Si Phase {N} n'est pas substantiellement
terminée, le rapport doit le déclarer en tête (avertissement d'honnêteté).
