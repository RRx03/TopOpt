# Plan de réconciliation documentaire

*Produit en Étape 3 (2026-06-26). **Ce document est un PLAN.** Aucune action
d'archivage/mise à jour n'est exécutée avant validation utilisateur ; l'exécution
a lieu en Étape 6. Chaque décision est justifiée.*

Légende : **GARDER** (inchangé) · **MAJ** (édité pour cohérence) ·
**ARCHIVER** (déplacé en `_legacy/`, jamais supprimé) · **CRÉER** (nouveau).

---

## Décision pivot : ROADMAP.md vs TRANSITIONS.md (anti-duplication)

`TopOptP1/TRANSITIONS.md` (516 L) est déjà la cartographie détaillée des 5 phases.
Dupliquer son contenu dans 4 briefs créerait un risque de divergence (drapeau rouge).

**Résolution actée :**
- `orchestration/ROADMAP.md` = **vue d'ensemble courte** (1-2 p.), pointe vers les briefs.
- `orchestration/prompts/PHASE_N_BRIEF.md` = **détail opérationnel par phase** (sections
  A–G), qui **supersède** l'usage opérationnel des sections par-phase de TRANSITIONS.
- `TopOptP1/TRANSITIONS.md` = **GARDER** comme référence historique, avec un **bandeau
  d'en-tête** ajouté pointant vers `orchestration/` comme source désormais autoritative.
  (Édition minime = MAJ légère, contenu intact.)

Ainsi : une seule source autoritative par usage, pas de copie intégrale dupliquée.

---

## Structure documentaire cible

```
[racine]/
├── TopOptP1/                          (code INTACT)
│   ├── CLAUDE.md                       MAJ  → overrides Phase 1 + pointeur MASTER
│   ├── README.md                       GARDER
│   ├── TRANSITIONS.md                  MAJ légère (bandeau "réf. historique")
│   ├── PHASE_1_REPORT.md               GARDER (revue cohérence en Étape 5)
│   └── _legacy/
│       ├── CLAUDE.original.md          ARCHIVER (sauvegarde avant slim)
│       ├── PHASE_1_TO_PHASE_2_HANDOFF.md  ARCHIVER (→ handoffs/PHASE_1_TO_2.md)
│       └── PHASE_2_INITIAL_PROMPT.md   ARCHIVER (mal placé, superseded)
├── TopOptP2/                          (code INTACT)
│   ├── CLAUDE.md                       MAJ  → overrides Phase 2 + pointeur MASTER
│   ├── PHASE_2_REPORT.md               GARDER (honnête, exact)
│   ├── STATUS.md, TASKS.md             GARDER
│   ├── docs/{ARCHITECTURE,DECISIONS,SYMBOLS}.md  GARDER
│   └── _legacy/
│       └── CLAUDE.original.md          ARCHIVER (sauvegarde avant slim)
├── orchestration/
│   ├── README.md                       MAJ  → mode d'emploi SANS scripts
│   ├── MASTER_CLAUDE.md                MAJ  → refonte (conventions réelles + 4 sections)
│   ├── VISION.md                       CRÉER
│   ├── ROADMAP.md                      CRÉER
│   ├── LESSONS_LEARNED.md              MAJ  → reformat + LL-002 (handoff inexistant)
│   ├── prompts/
│   │   ├── START_PHASE_N.md            CRÉER (template "un prompt suffit")
│   │   ├── CLOSE_PHASE_N.md            CRÉER (template clôture)
│   │   ├── PHASE_3_BRIEF.md            CRÉER (depuis PHASE_3_INITIAL_PROMPT + TRANSITIONS)
│   │   ├── PHASE_4_BRIEF.md            CRÉER
│   │   ├── PHASE_5_BRIEF.md            CRÉER
│   │   └── PHASE_6_BRIEF.md            CRÉER (reste template d'options)
│   ├── handoffs/
│   │   ├── PHASE_1_TO_2.md             CRÉER (rétroactif, depuis analyse)
│   │   └── PHASE_2_TO_3.md             CRÉER (rétroactif, P2 honnêtement incomplète)
│   └── _legacy/
│       ├── PHASE_3_INITIAL_PROMPT.md   ARCHIVER (→ PHASE_3_BRIEF.md)
│       ├── PHASE_4_INITIAL_PROMPT.md   ARCHIVER
│       ├── PHASE_5_INITIAL_PROMPT.md   ARCHIVER
│       ├── PHASE_6_INITIAL_PROMPT.md   ARCHIVER
│       ├── PHASE_N_HANDOFF_TEMPLATE.md ARCHIVER (→ CLOSE_PHASE_N + report template)
│       └── tools/{start,close,update_*}.sh  ARCHIVER (décision "no scripts")
├── shared/                            (INTACT — migration déjà faite)
│   ├── third_party/{eigen,metal-cpp,nlohmann,stb}
│   └── benchmarks/problem_mbb.json
└── analysis/
    ├── CODE_ANALYSIS.md                (Étape 2)
    ├── RECONCILIATION.md               (ce fichier)
    └── RESET_REPORT.md                 CRÉER (Étape 7)
```

---

## Tableau de décision par fichier existant

### orchestration/ (produits ma session précédente)
| Fichier | Décision | Justification |
|---|---|---|
| `MASTER_CLAUDE.md` | **MAJ** | Refonte : conventions = code réel (4 espaces, LDLT, Emin), + sections Vision/Positionnement/Principes/Gouvernance + protocole de session. |
| `LESSONS_LEARNED.md` | **MAJ** | Garder LL-001 + LL-LIT-*. Ajouter LL-002 (handoff inexistant, proposé par PHASE_2_REPORT). Renuméroter proprement. |
| `README.md` | **MAJ** | Réécrire : mécanisme "un prompt suffit" par templates Markdown, plus aucune référence aux scripts. |
| `PHASE_3_INITIAL_PROMPT.md` | **ARCHIVER** | Contenu migré et restructuré (A–G) dans `prompts/PHASE_3_BRIEF.md`. |
| `PHASE_4_INITIAL_PROMPT.md` | **ARCHIVER** | → `PHASE_4_BRIEF.md`. |
| `PHASE_5_INITIAL_PROMPT.md` | **ARCHIVER** | → `PHASE_5_BRIEF.md`. |
| `PHASE_6_INITIAL_PROMPT.md` | **ARCHIVER** | → `PHASE_6_BRIEF.md`. |
| `PHASE_N_HANDOFF_TEMPLATE.md` | **ARCHIVER** | Scindé : template clôture → `CLOSE_PHASE_N.md` ; gabarit rapport repris dans la procédure. |
| `tools/start_phase.sh` | **ARCHIVER** | Décision design "no scripts" ; logique migrée dans `START_PHASE_N.md`. |
| `tools/close_phase.sh` | **ARCHIVER** | → `CLOSE_PHASE_N.md`. |
| `tools/update_lessons.sh` | **ARCHIVER** | Remplacé par instruction Markdown dans le protocole de session. |

### TopOptP1/ (.md uniquement — CODE INTACT)
| Fichier | Décision | Justification |
|---|---|---|
| `CLAUDE.md` | **MAJ** (+ archive de l'original) | Devient des overrides Phase 1 minces pointant vers MASTER. Original sauvegardé `_legacy/CLAUDE.original.md`. |
| `README.md` | **GARDER** | Description Phase 1 exacte. |
| `TRANSITIONS.md` | **MAJ légère** | Bandeau d'en-tête "référence historique, voir orchestration/". Contenu intact. |
| `PHASE_1_REPORT.md` | **GARDER** | Revue de cohérence en Étape 5 ; correction seulement si écart factuel. |
| `PHASE_1_TO_PHASE_2_HANDOFF.md` | **ARCHIVER** | Remplacé par `orchestration/handoffs/PHASE_1_TO_2.md` (rétroactif, aligné code réel). |
| `PHASE_2_INITIAL_PROMPT.md` | **ARCHIVER** | Mal placé (prompt P2 sous P1) et superseded par les templates. |

### TopOptP2/ (.md uniquement — CODE INTACT)
| Fichier | Décision | Justification |
|---|---|---|
| `CLAUDE.md` | **MAJ** (+ archive de l'original) | Overrides Phase 2 minces + pointeur MASTER. Original `_legacy/CLAUDE.original.md`. |
| `PHASE_2_REPORT.md` | **GARDER** | Rapport honnête et exact (1/9). |
| `STATUS.md` | **GARDER** | Doc de travail de phase. |
| `TASKS.md` | **GARDER** | Doc de travail de phase. |
| `docs/ARCHITECTURE.md` | **GARDER** | Décrit la fondation Metal réelle. |
| `docs/DECISIONS.md` | **GARDER** | ADR-001..004 valides. |
| `docs/SYMBOLS.md` | **GARDER** | Index (à enrichir quand le code 3D arrivera). |

---

## Décisions matérielles (tranchées par l'utilisateur, Étape 3)

| Item | Décision actée |
|---|---|
| `metal-cpp/` racine | **LAISSER TEL QUEL.** Dossier personnel de l'utilisateur (copier-coller vers projets perso) ; aucun projet ne pointe dessus. Pas un doublon à résoudre. Aucune action. |
| Emplacement `_legacy/` | **Central : `analysis/_legacy/`** (un seul dossier d'archive, organisé par origine). Remplace les `_legacy/` par-phase. |
| Repo git racine | **CRÉER** un repo git à la racine + `.gitignore` (Étape 6). Les repos par-phase (P1/P2) existent déjà ; le repo racine les ignore (pas de sous-modules) et versionne `orchestration/`, `analysis/`, `shared/` (deps vendorisées exclues). |

### Emplacement d'archivage révisé (central)
```
analysis/_legacy/
├── orchestration/
│   ├── PHASE_3_INITIAL_PROMPT.md ... PHASE_6_INITIAL_PROMPT.md
│   ├── PHASE_N_HANDOFF_TEMPLATE.md
│   └── tools/{start_phase,close_phase,update_lessons}.sh
├── TopOptP1/
│   ├── CLAUDE.original.md
│   ├── PHASE_1_TO_PHASE_2_HANDOFF.md
│   └── PHASE_2_INITIAL_PROMPT.md
└── TopOptP2/
    └── CLAUDE.original.md
```

### .gitignore racine prévu
```
build/        *.o        *.dSYM        .DS_Store
shared/third_party/    # deps vendorisées (lourdes, non versionnées)
TopOptP1/    TopOptP2/  # repos git autonomes (snapshots baseline propres)
```
*(Les phases gardent leur propre historique git ; le repo racine pilote l'outillage.)*

---

## Principes de la réconciliation

1. **Le code ne bouge pas.** Seuls les `.md` sont touchés.
2. **Rien n'est supprimé** : tout fichier écarté part en `_legacy/` (récupérable + git baseline).
3. **Une source autoritative par usage** : pas de duplication de contenu entre
   TRANSITIONS, ROADMAP et briefs.
4. **Le code fait foi** : toute convention écrite reflète l'implémentation réelle
   (cf. divergences D1–D9 de CODE_ANALYSIS.md).
5. **Honnêteté d'état** : le handoff P2→P3 dira que Phase 2 est incomplète (1/9).
