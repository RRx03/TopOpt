# RESET_REPORT.md — Bilan du reset documentaire TopOpt

*Produit le 2026-06-26. Synthèse de la session de remise au propre de
l'architecture documentaire. Aucun code applicatif modifié.*

---

## 1. Objectif de la session

Repartir sur des bases documentaires saines après accumulation de prompts,
scripts et fichiers ad-hoc ayant créé des incohérences. Méthode : analyse
archéologique du **code réel** (source de vérité), régénération des passations,
architecture documentaire unifiée, mécanisme "un prompt suffit" en Markdown.

---

## 2. Inventaire avant reset (Étape 0)

- **Code** : Phase 1 (~716 LOC, validée, tests verts), Phase 2 (~243 LOC,
  fondation Metal seule ~1/9). Migration `shared/third_party/` déjà faite
  (symlinks depuis P1/P2). Aucun git nulle part.
- **Docs** : 21 fichiers `.md` répartis P1 (6), P2 (7), orchestration (8) +
  3 scripts shell. Incohérences relevées : prompt P2 rangé sous P1, doublon de
  cartographie (TRANSITIONS vs futur ROADMAP), README orchestration décrivant des
  scripts à supprimer, doc↔code divergente.

---

## 3. Analyse archéologique (Étape 2) — faits clés

Détail : `analysis/CODE_ANALYSIS.md`. **Le code fait foi.** 9 divergences
documentation↔code relevées (aucune n'est un bug), dont les majeures :
- D1 : solveur réel `SimplicialLDLT` (docs disaient LLT).
- D2 : ρ∈[0,1] + plancher `Emin=1e-9` (docs disaient ρ_min=1e-3).
- D3 : pas de continuation de p (p fixé=3).
- D6 : modules fusionnés (FEM2D, SIMP) vs ~15 modules annoncés.
- D7 : 1 fichier de test (traction+filtre+MBB) vs 3 annoncés ; pas de patch test vrai.

Tests re-exécutés : P1 ALL PASS (traction 4.8e-14, filtre 3.3e-16, MBB 1007→242),
P2 vec_add GPU/CPU exact.

---

## 4. Décisions de conservation/archivage (Étapes 3 & 6)

Détail : `analysis/RECONCILIATION.md`. Principe : rien supprimé, tout écarté →
`analysis/_legacy/` (central). Arbitrages utilisateur :
- `metal-cpp/` racine : **laissé tel quel** (dossier perso, aucun projet n'y pointe).
- `_legacy/` : **central** sous `analysis/_legacy/`.
- Git : **repo racine créé** + repos par phase (baseline pré-reset).

Archivés : 4× `PHASE_N_INITIAL_PROMPT.md`, `PHASE_N_HANDOFF_TEMPLATE.md`,
3× `tools/*.sh`, `PHASE_1_TO_PHASE_2_HANDOFF.md`, `PHASE_2_INITIAL_PROMPT.md`,
2× `CLAUDE.original.md`.

---

## 5. Nouveaux documents créés (Étapes 4 & 5)

```
orchestration/
├── VISION.md                  positionnement (Noyron, nTop, Altair, dolfin-adjoint...)
├── ROADMAP.md                 vue d'ensemble 5 phases + statut réel
├── MASTER_CLAUDE.md           REFONDU : conventions = code réel + protocole + gouvernance
├── LESSONS_LEARNED.md         MAJ : LL-001..005 + LL-LIT-001..012
├── README.md                  RÉÉCRIT : sans scripts, hiérarchie des sources
├── prompts/
│   ├── START_PHASE_N.md       template "un prompt suffit" (démarrage)
│   ├── CLOSE_PHASE_N.md       template clôture
│   └── PHASE_{3,4,5,6}_BRIEF.md  briefs scientifiques (sections A–G + pseudo-code)
└── handoffs/
    ├── PHASE_1_TO_2.md        rétroactif, ancré code réel
    └── PHASE_2_TO_3.md        rétroactif, avertissement P2 incomplète
```
Rapports réconciliés : `PHASE_1_REPORT.md` (renvoi LESSONS corrigé),
`PHASE_2_REPORT.md` (conservé, déjà honnête).

---

## 6. Vérification de cohérence (Étape 7)

- **Références croisées** : toutes les `MANQUANT` détectées sont soit des
  placeholders de templates (`{N}`), soit des fichiers futurs attendus
  (`RESET_REPORT.md`, `TopOptP5/PHASE_5_REPORT.md`), soit des mentions
  intentionnelles d'un fichier inexistant (leçon LL-002). **Aucun lien cassé.**
- **Notation handoff** harmonisée dans MASTER (`PHASE_{N-1}_TO_{N}.md`).
- **Dry-run "un prompt suffit"** (`START_PHASE_3`, {N}→3) : toutes les lectures
  résolvent ; le drapeau rouge préalable détecte correctement que Phase 2 est
  incomplète et **stoppe** — comportement voulu (règle d'or respectée).
- **Builds après édits** : P1 ALL PASS, P2 GPU/CPU exact (les `.md` n'affectent
  pas le code).

---

## 7. Trous identifiés et action proposée

| Trou | Impact | Action |
|---|---|---|
| **Phase 2 réellement à 1/9** | Phase 3 ne peut pas démarrer | Terminer le solveur 3D GPU avant `START_PHASE_3`. Documenté partout (handoff, brief, CLAUDE P2). |
| Pas de patch test FEM vrai (P1) | méta-règle non satisfaite stricto sensu | Ajouter un patch test (champ uniforme) quand le FEM est étendu (P2/3). |
| `PHASE_6_BRIEF` = template d'options | normal | À instancier à la clôture de Phase 5. |
| Repos git imbriqués (racine + phases) | racine ignore P1/P2 | Assumé : phases = repos autonomes ; racine = outillage. |

---

## 8. Mode d'emploi pour Phase 3 (concret)

> ⚠️ **Prérequis** : terminer d'abord le solveur 3D GPU de Phase 2 (cf.
> `orchestration/handoffs/PHASE_2_TO_3.md`, reste-à-faire). Tant que Phase 2 est à
> 1/9, le démarrage de Phase 3 sera (à juste titre) bloqué par le drapeau rouge.

Quand Phase 2 sera complète et validée :
1. Ouvrir `orchestration/prompts/START_PHASE_N.md`.
2. Remplacer `{N}` → `3` et `{N-1}` → `2` dans tout le bloc "PROMPT À DONNER".
3. Coller dans une session Claude Code fraîche à la racine
   (`/Users/romanroux/Dev/Metal`).
4. La session lira MASTER/VISION/ROADMAP/LESSONS + `handoffs/PHASE_2_TO_3.md` +
   `prompts/PHASE_3_BRIEF.md` + `analysis/CODE_ANALYSIS.md`, vérifiera les
   prérequis, créera `TopOptP3/`, et présentera le plan (5 sessions).

Pour clôturer une phase : même principe avec `prompts/CLOSE_PHASE_N.md`.

---

## 9. État git final

| Repo | Commit de tête |
|---|---|
| racine | `10f0fe0` chore: documentary orchestration reset |
| TopOptP1 | `3b97def` docs(p1): slim CLAUDE, archive legacy, transitions banner |
| TopOptP2 | `9d78e43` docs(p2): slim CLAUDE to overrides + master pointer |

Baselines pré-reset conservées (P1 `c4529db`, P2 `eaf3939`) pour rollback.

---

## 10. Conclusion

Architecture documentaire unifiée, cohérente, ancrée sur le code réel. Mécanisme
"un prompt suffit" opérationnel et testé en dry-run. Honnêteté d'état préservée
(Phase 2 incomplète signalée partout). **Aucun code applicatif modifié** ;
builds et tests toujours verts.
