#!/usr/bin/env bash
# start_phase.sh — Démarrer une nouvelle phase TopOpt
# Usage : ./orchestration/tools/start_phase.sh <N>
#
# Ce script :
# 1. Vérifie que la phase précédente est clôturée (rapport + TRANSITIONS.md)
# 2. Crée TopOptPN/ avec la structure de base
# 3. Affiche le PHASE_N_INITIAL_PROMPT.md à copier dans Claude Code

set -euo pipefail

# ── Couleurs ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'
YEL='\033[1;33m'
GRN='\033[0;32m'
CYN='\033[0;36m'
RST='\033[0m'

err()  { echo -e "${RED}[ERREUR]${RST} $*" >&2; }
warn() { echo -e "${YEL}[WARN]${RST}  $*"; }
ok()   { echo -e "${GRN}[OK]${RST}    $*"; }
info() { echo -e "${CYN}[INFO]${RST}  $*"; }

# ── Argument ──────────────────────────────────────────────────────────────────
if [[ $# -ne 1 ]] || ! [[ $1 =~ ^[0-9]+$ ]]; then
  err "Usage : $0 <numéro_de_phase>"
  err "Exemple : $0 3"
  exit 1
fi

N=$1
PREV=$((N - 1))
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ORCH_DIR="$ROOT_DIR/orchestration"

# ── Chemins ───────────────────────────────────────────────────────────────────
PREV_DIR="$ROOT_DIR/TopOptP${PREV}"
NEW_DIR="$ROOT_DIR/TopOptP${N}"
PREV_REPORT="$PREV_DIR/PHASE_${PREV}_REPORT.md"
TRANSITIONS="$ROOT_DIR/TopOptP1/TRANSITIONS.md"
PROMPT_FILE="$ORCH_DIR/PHASE_${N}_INITIAL_PROMPT.md"
LESSONS="$ORCH_DIR/LESSONS_LEARNED.md"

echo ""
echo "═══════════════════════════════════════════════════════"
echo "  TopOpt — Démarrage Phase ${N}"
echo "═══════════════════════════════════════════════════════"
echo ""

# ── Vérifications préalables ──────────────────────────────────────────────────
ERRORS=0

# Phase précédente existe ?
if [[ ! -d "$PREV_DIR" ]]; then
  err "TopOptP${PREV}/ introuvable à $PREV_DIR"
  err "La phase précédente doit exister avant de démarrer la phase $N"
  ERRORS=$((ERRORS + 1))
else
  ok "TopOptP${PREV}/ trouvé"
fi

# Rapport de clôture existe ?
if [[ ! -f "$PREV_REPORT" ]]; then
  warn "PHASE_${PREV}_REPORT.md absent de TopOptP${PREV}/"
  warn "Phase ${N} ne peut démarrer que si Phase ${PREV} est officiellement clôturée"
  warn "Crée ce fichier à partir de orchestration/PHASE_N_HANDOFF_TEMPLATE.md"
  ERRORS=$((ERRORS + 1))
else
  ok "PHASE_${PREV}_REPORT.md trouvé"
fi

# TRANSITIONS.md existe ?
if [[ ! -f "$TRANSITIONS" ]]; then
  err "TopOptP1/TRANSITIONS.md introuvable"
  ERRORS=$((ERRORS + 1))
else
  ok "TRANSITIONS.md trouvé"
fi

# LESSONS_LEARNED.md existe ?
if [[ ! -f "$LESSONS" ]]; then
  err "orchestration/LESSONS_LEARNED.md introuvable"
  ERRORS=$((ERRORS + 1))
else
  ok "LESSONS_LEARNED.md trouvé"
fi

# Prompt initial existe ?
if [[ ! -f "$PROMPT_FILE" ]]; then
  warn "PHASE_${N}_INITIAL_PROMPT.md introuvable"
  warn "Si Phase $N > 6 : créer le prompt à partir du template PHASE_N_HANDOFF_TEMPLATE.md"
  ERRORS=$((ERRORS + 1))
fi

# Phase déjà créée ?
if [[ -d "$NEW_DIR" ]]; then
  warn "TopOptP${N}/ existe déjà à $NEW_DIR"
  warn "Skipping création de la structure (le dossier sera utilisé tel quel)"
  SKIP_CREATE=1
else
  SKIP_CREATE=0
fi

# ── Arrêt si erreurs bloquantes ───────────────────────────────────────────────
if [[ $ERRORS -gt 0 ]]; then
  echo ""
  err "$ERRORS problème(s) bloquant(s) détecté(s). Corriger avant de continuer."
  echo ""
  exit 1
fi

echo ""
info "Toutes les vérifications préalables passées."
echo ""

# ── Création de la structure TopOptPN/ ───────────────────────────────────────
if [[ $SKIP_CREATE -eq 0 ]]; then
  info "Création de TopOptP${N}/..."
  mkdir -p "$NEW_DIR"/{src/{core,fem,topopt,filter,physics,adjoint,metal,io,benchmarks},shaders,tests,docs,build,assets}

  # CLAUDE.md local (stub à compléter)
  cat > "$NEW_DIR/CLAUDE.md" << CLAUDEMD
# TopOpt — Phase ${N}

## Purpose
[À remplir — décrire l'objectif de cette phase en 1-2 phrases]

## Stack
C++23, clang, macOS Apple Silicon
metal-cpp (header-only) vendorisé dans third_party/metal-cpp
Build : Makefile two-phase (shaders → metallib, puis C++ + link)

## Architecture (brief)
[À remplir après session 1]
→ Détails : \`docs/ARCHITECTURE.md\`

## Directory map
- \`src/core/\`    : structures de grille, maillage
- \`src/fem/\`     : assembly, solveurs linéaires
- \`src/topopt/\`  : SIMP, OC/MMA, objectifs
- \`src/filter/\`  : Helmholtz, Heaviside
- \`src/physics/\` : physiques additionnelles (thermique, Stokes...)
- \`src/adjoint/\` : adjoint multi-bloc
- \`src/metal/\`   : contexte GPU, wrappers Metal
- \`shaders/\`     : kernels .metal
- \`tests/\`       : tests unitaires autonomes

## Commands
- Build : \`make\`
- Test  : \`make test\`
- Run   : \`make run\`
- Clean : \`make clean\`

## Read first every session
1. \`../orchestration/MASTER_CLAUDE.md\`
2. \`../orchestration/LESSONS_LEARNED.md\`
3. Ce fichier
4. \`STATUS.md\`

## Read on demand
- \`docs/ARCHITECTURE.md\` — modification structurelle
- \`docs/SYMBOLS.md\` — localiser un symbole
- \`docs/DECISIONS.md\` — comprendre un choix passé
- \`TASKS.md\` — prioriser
- \`../TopOptP1/TRANSITIONS.md\` — sections Phase ${N} et suivantes

## Project-specific rules
[À remplir — règles spécifiques à cette phase]

## Gotchas
[À remplir au fur et à mesure]
CLAUDEMD

  # STATUS.md initial
  cat > "$NEW_DIR/STATUS.md" << STATUSMD
# Status — $(date +%Y-%m-%d)

## Current focus
Démarrage Phase ${N} — session 1

## Last session ($(date +%Y-%m-%d))
- Création structure TopOptP${N}/
- Commit: WIP

## Next up
1. Lire MASTER_CLAUDE.md + LESSONS_LEARNED.md
2. Lire TRANSITIONS.md section Phase ${N}
3. Présenter plan d'architecture AVANT de coder

## Blockers
- Aucun

## WIP files
- Aucun encore

## Don't touch
- ../TopOptP1/ (archivée)
- ../TopOptP2/ (archivée)
- ../TopOptP$((N-1))/ (archivée)
STATUSMD

  # TASKS.md initial
  cat > "$NEW_DIR/TASKS.md" << TASKSMD
# Tasks — Phase ${N}

## Active
- [ ] Lire MASTER_CLAUDE.md et LESSONS_LEARNED.md — session 1
- [ ] Lire TRANSITIONS.md section Phase ${N} — session 1
- [ ] Présenter plan d'architecture au format [CONCEPT][CHOIX] — session 1
- [ ] [Remplir selon PHASE_${N}_INITIAL_PROMPT.md, priorités session 1]

## Done (last 14 days)
- [x] $(date +%Y-%m-%d) : Structure TopOptP${N}/ créée par start_phase.sh

## Backlog
- [ ] [Remplir selon les acquis attendus de PHASE_${N}_INITIAL_PROMPT.md]

## Git log (recent)
- (aucun commit Phase ${N} encore)
TASKSMD

  # Stub docs/
  cat > "$NEW_DIR/docs/ARCHITECTURE.md" << ARCHDOC
# Architecture — Phase ${N}
[À remplir après session 1]
ARCHDOC

  cat > "$NEW_DIR/docs/SYMBOLS.md" << SYMDOC
# Symbols index — Phase ${N}
Last updated: $(date +%Y-%m-%d)

## Classes / Structs
[À remplir au fur et à mesure]

## Key functions
[À remplir au fur et à mesure]
SYMDOC

  cat > "$NEW_DIR/docs/DECISIONS.md" << DECDOC
# Architecture Decision Records — Phase ${N}

[Les ADR sont numérotés dans la continuité des phases précédentes]
[Format : cf. MASTER_CLAUDE.md section "Format des ADR"]
DECDOC

  # Liens symboliques vers les dépendances et benchmarks partagés (shared/)
  SHARED_TP="$ROOT_DIR/shared/third_party"
  if [[ -d "$SHARED_TP" ]]; then
    mkdir -p "$NEW_DIR/third_party"
    for dep in "$SHARED_TP"/*/; do
      [[ -d "$dep" ]] || continue
      depname="$(basename "$dep")"
      ln -sfn "../../shared/third_party/$depname" "$NEW_DIR/third_party/$depname"
      info "third_party/$depname -> shared/third_party/$depname"
    done
    ok "Dépendances partagées liées depuis shared/third_party/"
  else
    warn "shared/third_party/ introuvable — dépendances à configurer manuellement"
  fi

  if [[ -d "$ROOT_DIR/shared/benchmarks" ]]; then
    ln -sfn "../shared/benchmarks" "$NEW_DIR/benchmarks"
    info "benchmarks -> shared/benchmarks"
  fi

  # .gitignore minimal
  cat > "$NEW_DIR/.gitignore" << GITIGNORE
build/
*.o
*.dSYM
.DS_Store
xcuserdata/
DerivedData/
GITIGNORE

  ok "TopOptP${N}/ créé avec la structure de base"
fi

# ── Affichage du prompt ───────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════"
echo "  PROMPT À COPIER DANS CLAUDE CODE (TopOptP${N}/)"
echo "═══════════════════════════════════════════════════════"
echo ""

if [[ -f "$PROMPT_FILE" ]]; then
  cat "$PROMPT_FILE"
else
  warn "Fichier PHASE_${N}_INITIAL_PROMPT.md introuvable."
  info "Crée-le à partir du template et relance ce script."
fi

echo ""
echo "═══════════════════════════════════════════════════════"
echo "  Copier ce prompt comme PREMIER MESSAGE dans Claude Code"
echo "  dans le dossier TopOptP${N}/"
echo "═══════════════════════════════════════════════════════"
echo ""
ok "start_phase.sh terminé — bonne session !"
echo ""
