# orchestration/ — Mode d'emploi

Infrastructure documentaire de pilotage du projet TopOpt. **Pas de code, pas de
scripts** : uniquement des documents et des templates Markdown à copier-coller.
Mécanisme « un prompt suffit ».

---

## Contenu

```
orchestration/
├── README.md            ce fichier
├── VISION.md            pourquoi le projet, positionnement vs alternatives
├── ROADMAP.md           vue d'ensemble des 5 phases + statut
├── MASTER_CLAUDE.md     autorité commune (identité, conventions, protocole)
├── LESSONS_LEARNED.md   erreurs et pièges accumulés (vivant)
├── prompts/
│   ├── START_PHASE_N.md   template de démarrage de phase
│   ├── CLOSE_PHASE_N.md   template de clôture de phase
│   └── PHASE_{3,4,5,6}_BRIEF.md  briefs scientifiques par phase
└── handoffs/
    ├── PHASE_1_TO_2.md    passation rétroactive
    └── PHASE_2_TO_3.md    passation rétroactive (P2 honnêtement incomplète)
```

Documents d'analyse associés (à la racine) : `analysis/CODE_ANALYSIS.md`
(faits sur le code réel), `analysis/RECONCILIATION.md` (décisions documentaires),
`analysis/RESET_REPORT.md` (bilan du reset).

---

## Démarrer une phase (« un prompt suffit »)

1. Vérifier que la phase précédente est **réellement** terminée (rapport +
   tests verts, pas seulement l'existence des fichiers).
2. Ouvrir `prompts/START_PHASE_N.md`, copier le bloc « PROMPT À DONNER À CLAUDE
   CODE », remplacer `{N}` par le numéro et `{N-1}` par N−1.
3. Coller dans une session Claude Code **fraîche** à la racine du projet.

Le prompt enchaîne lui-même : lectures obligatoires → diagnostic des prérequis →
création de `TopOptPN/` → CLAUDE.md de phase → plan → premier module.

## Clôturer une phase

1. Ouvrir `prompts/CLOSE_PHASE_N.md`, remplacer `{N}` et `{N+1}`.
2. Coller dans la session de phase.
3. Le prompt produit : `PHASE_N_REPORT.md`, `handoffs/PHASE_N_TO_(N+1).md`,
   mise à jour `LESSONS_LEARNED.md`, et propose le commit de clôture.

## Mettre à jour LESSONS_LEARNED.md

À la **fin de chaque session** (pas seulement fin de phase) : ajouter les pièges
rencontrés, format `LL-XXX : Titre (Phase N, AAAA-MM-JJ)` avec
Symptôme / Cause / Conséquence / Leçon / Vérification. Si rien : l'écrire.

Numérotation : `LL-0xx` pour les erreurs de session, `LL-LIT-0xx` pour les pièges
connus de la littérature.

---

## Qu'est-ce qu'un handoff ?

La passation entre deux phases. Il contient : l'état réel du code en sortie, les
modifications requises pour la phase suivante, l'architecture cible, les pièges
anticipés, les validations obligatoires. Les handoffs vivent dans
`orchestration/handoffs/` (et non dans les dossiers de phase).

---

## Hiérarchie des sources (qui fait foi)

1. **Le code** (`TopOptPN/`) — vérité absolue. `analysis/CODE_ANALYSIS.md` en fait l'inventaire factuel.
2. **MASTER_CLAUDE.md** — conventions et protocole (alignés sur le code).
3. **PHASE_N_BRIEF.md** — détail opérationnel de la phase courante.
4. **handoffs/** — état de transition entre phases.
5. **TRANSITIONS.md** (dans `TopOptP1/`) — cartographie historique de référence,
   superseded opérationnellement par les briefs.

En cas de conflit, l'ordre ci-dessus tranche : **le code l'emporte sur la doc**.

---

## Règles de modification

- **Ne jamais toucher au code applicatif** depuis l'orchestration.
- Documents vivants (à mettre à jour) : `LESSONS_LEARNED.md`, `TRANSITIONS.md`,
  les `STATUS.md`/`TASKS.md` de la phase en cours.
- Documents stables : `VISION.md`, `ROADMAP.md`, `MASTER_CLAUDE.md` (changer
  seulement sur décision structurelle).
- Tout document écarté part dans `analysis/_legacy/`, jamais supprimé.
