# START_PHASE_N.md — Template "un prompt suffit" (démarrage de phase)

## INSTRUCTIONS POUR L'UTILISATEUR

Pour démarrer la phase N :
1. Copier **tout le bloc** sous « PROMPT À DONNER À CLAUDE CODE ».
2. Remplacer chaque `{N}` par le numéro réel (3, 4, 5, 6) et `{N-1}` par N−1.
3. Coller dans une session Claude Code **fraîche**, lancée à la racine
   (`/Users/romanroux/Dev/Metal`).

Rien d'autre n'est requis : toutes les lectures et actions découlent du prompt.

---

# PROMPT À DONNER À CLAUDE CODE

Tu démarres la **Phase {N}** du projet TopOpt. Tu es à la racine du projet.

## LECTURES OBLIGATOIRES (dans l'ordre, avant toute action)

1. `orchestration/MASTER_CLAUDE.md` — règles globales, conventions, protocole de session
2. `orchestration/VISION.md` — contexte et positionnement du projet
3. `orchestration/ROADMAP.md` — vue d'ensemble des phases
4. `orchestration/LESSONS_LEARNED.md` — pièges connus (à relire à chaque session)
5. `orchestration/handoffs/PHASE_{N-1}_TO_{N}.md` — état réel en début de phase
6. `orchestration/prompts/PHASE_{N}_BRIEF.md` — brief scientifique de cette phase
7. `analysis/CODE_ANALYSIS.md` — faits sur le code réel (le code fait foi)

Confirme par une synthèse de 5-8 lignes : objectif Phase {N}, prérequis attendus,
et **vérifie que les prérequis de la phase {N-1} sont réellement satisfaits**
(ne pas se fier au nom des fichiers — vérifier le code et les tests).

## DRAPEAU ROUGE PRÉALABLE

Avant de créer quoi que ce soit, vérifie que la phase {N-1} est **réellement
terminée** (checkpoints verts dans son `PHASE_{N-1}_REPORT.md` ET tests qui
passent). Si la phase précédente est incomplète (ex. Phase 2 actuellement à ~1/9),
**STOP** et signale-le : la règle d'or interdit de démarrer la phase suivante sur
une base non validée.

## ACTIONS À EXÉCUTER (après validation utilisateur du diagnostic)

### Étape A — Création du dossier de phase
- Créer `TopOptP{N}/` à la racine.
- Reprendre l'**arborescence de base** de `TopOptP{N-1}/` (Makefile, `src/`,
  `tests/`, `docs/`, `shaders/` si pertinent) — **structure, pas copie du code
  spécifique** sauf réutilisation explicitement justifiée.
- Créer les symlinks `third_party/` vers `shared/third_party/` (deps partagées) :
  un lien par dépendance utilisée (`eigen`, `metal-cpp`, `nlohmann`, `stb`).
- `git init` dans `TopOptP{N}/` + commit initial "Phase {N} skeleton".

### Étape B — CLAUDE.md de phase
Créer `TopOptP{N}/CLAUDE.md` **mince** :
- Référence `../orchestration/MASTER_CLAUDE.md` comme autorité.
- Liste uniquement les **overrides** Phase {N} : priorités, composants nouveaux,
  points d'attention spécifiques (issus du brief).
- Liste les lectures « first session » et « on demand ».

### Étape C — Plan de phase
- Présenter le plan détaillé issu de `PHASE_{N}_BRIEF.md`.
- Décomposer en **sessions de travail** prévisibles (cf. section "Décomposition"
  du brief), chacune avec un livrable testable.
- Lister les **validations obligatoires de fin de phase** (tolérances chiffrées).

### Étape D — Premier module
- Identifier le **premier module à coder** (typiquement adaptation du code
  Phase {N-1} ou brique simple isolée).
- Présenter `[CONCEPT][CHOIX]` puis le plan d'implémentation.
- **Attendre la validation utilisateur avant d'implémenter.**

## CONTRAINTES
- **Aucune modification** du code de `TopOptP{N-1}` ni des phases antérieures.
- Conventions du `MASTER_CLAUDE.md` respectées (4 espaces, namespaces, etc.).
- Commits Git fréquents et explicites (Conventional Commits) ; **jamais committer
  sans validation**.
- Format de réponse standardisé `[CONCEPT][CHOIX][CODE][COMPLEXITÉ][VALIDATION][LIMITES]`.
- Validation incrémentale par l'utilisateur à chaque étape majeure.

## DRAPEAUX ROUGES (génériques + voir brief)
- Phase {N-1} non réellement terminée → STOP.
- Dépendance hors `shared/third_party/` → demander.
- Test de validation qui échoue → STOP, corriger avant d'avancer.
- + les drapeaux rouges **spécifiques** listés dans `PHASE_{N}_BRIEF.md`.

## VALIDATION DE FIN DE SESSION INITIALE
À l'issue de cette session de démarrage, doivent exister :
- `TopOptP{N}/` avec arborescence de base + symlinks `third_party/`
- `TopOptP{N}/CLAUDE.md` propre (overrides + pointeur MASTER)
- `git init` + premier commit "Phase {N} skeleton"
- Plan de travail validé par l'utilisateur
- Premier module identifié, plan d'implémentation présenté (pas encore codé sans accord)
