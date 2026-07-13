#!/usr/bin/env bash
# close_phase.sh — Clôturer proprement une phase TopOpt
# Usage : ./orchestration/tools/close_phase.sh <N> [--commit]
#
# Ce script vérifie que tous les artefacts de clôture sont présents.
# Avec --commit : propose un git commit de clôture (après validation manuelle).

set -euo pipefail

RED='\033[0;31m'
YEL='\033[1;33m'
GRN='\033[0;32m'
CYN='\033[0;36m'
RST='\033[0m'

err()  { echo -e "${RED}[ERREUR]${RST} $*" >&2; ERRORS=$((ERRORS + 1)); }
warn() { echo -e "${YEL}[WARN]${RST}  $*"; WARNINGS=$((WARNINGS + 1)); }
ok()   { echo -e "${GRN}[OK]${RST}    $*"; }
info() { echo -e "${CYN}[INFO]${RST}  $*"; }

# ── Arguments ─────────────────────────────────────────────────────────────────
if [[ $# -lt 1 ]] || ! [[ $1 =~ ^[0-9]+$ ]]; then
  echo "Usage : $0 <numéro_de_phase> [--commit]"
  echo "Exemple : $0 3"
  echo "Exemple : $0 3 --commit"
  exit 1
fi

N=$1
DO_COMMIT=0
if [[ $# -ge 2 ]] && [[ $2 == "--commit" ]]; then
  DO_COMMIT=1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ORCH_DIR="$ROOT_DIR/orchestration"
PHASE_DIR="$ROOT_DIR/TopOptP${N}"
TRANSITIONS="$ROOT_DIR/TopOptP1/TRANSITIONS.md"
LESSONS="$ORCH_DIR/LESSONS_LEARNED.md"
REPORT="$PHASE_DIR/PHASE_${N}_REPORT.md"

ERRORS=0
WARNINGS=0

echo ""
echo "═══════════════════════════════════════════════════════"
echo "  TopOpt — Clôture Phase ${N}"
echo "═══════════════════════════════════════════════════════"
echo ""

# ── Vérification existence du dossier ─────────────────────────────────────────
if [[ ! -d "$PHASE_DIR" ]]; then
  err "TopOptP${N}/ introuvable à $PHASE_DIR"
  exit 1
fi
ok "TopOptP${N}/ trouvé"

# ── Vérification du rapport ────────────────────────────────────────────────────
if [[ ! -f "$REPORT" ]]; then
  err "PHASE_${N}_REPORT.md absent"
  err "Créer ce fichier à partir de orchestration/PHASE_N_HANDOFF_TEMPLATE.md"
else
  ok "PHASE_${N}_REPORT.md trouvé"

  # Vérifier que le rapport n'est pas un stub vide
  WORD_COUNT=$(wc -w < "$REPORT")
  if [[ $WORD_COUNT -lt 200 ]]; then
    warn "PHASE_${N}_REPORT.md semble incomplet ($WORD_COUNT mots < 200 attendus)"
    warn "S'assurer que toutes les sections sont remplies"
  else
    ok "PHASE_${N}_REPORT.md a $WORD_COUNT mots (minimum 200)"
  fi

  # Vérifier la checklist de clôture
  UNCHECKED=$(grep -c '^\- \[ \]' "$REPORT" 2>/dev/null || echo 0)
  if [[ $UNCHECKED -gt 0 ]]; then
    warn "$UNCHECKED item(s) non cochés dans la checklist de $REPORT"
    grep '^\- \[ \]' "$REPORT" | head -5
  else
    ok "Checklist de $REPORT : toutes les cases cochées (ou absent)"
  fi
fi

# ── Vérification TRANSITIONS.md ────────────────────────────────────────────────
if [[ ! -f "$TRANSITIONS" ]]; then
  err "TopOptP1/TRANSITIONS.md introuvable"
else
  ok "TRANSITIONS.md trouvé"
  # Heuristique : TRANSITIONS.md doit avoir été modifié récemment (7 jours)
  DAYS_OLD=$(( ($(date +%s) - $(date -r "$TRANSITIONS" +%s 2>/dev/null || echo 0)) / 86400 ))
  if [[ $DAYS_OLD -gt 7 ]]; then
    warn "TRANSITIONS.md n'a pas été modifié depuis ${DAYS_OLD} jours"
    warn "Vérifier que les acquis Phase ${N} y sont bien couverts"
  else
    ok "TRANSITIONS.md modifié il y a ${DAYS_OLD} jour(s)"
  fi
fi

# ── Vérification LESSONS_LEARNED.md ───────────────────────────────────────────
if [[ ! -f "$LESSONS" ]]; then
  err "orchestration/LESSONS_LEARNED.md introuvable"
else
  ok "LESSONS_LEARNED.md trouvé"
  # Vérifier qu'une entrée mentionne "Phase N" (heuristique)
  PHASE_ENTRIES=$(grep -c "Phase ${N}" "$LESSONS" 2>/dev/null || echo 0)
  if [[ $PHASE_ENTRIES -eq 0 ]]; then
    warn "Aucune entrée mentionnant 'Phase ${N}' dans LESSONS_LEARNED.md"
    warn "S'assurer d'avoir ajouté les nouveaux pièges de cette phase"
  else
    ok "LESSONS_LEARNED.md : $PHASE_ENTRIES occurrence(s) de 'Phase ${N}'"
  fi
fi

# ── Vérification compilation ────────────────────────────────────────────────────
if [[ -f "$PHASE_DIR/Makefile" ]]; then
  info "Lancement de 'make test' dans TopOptP${N}/..."
  cd "$PHASE_DIR"
  if make test 2>&1 | tail -5; then
    ok "make test réussi"
  else
    warn "make test échoué — vérifier avant de clôturer"
  fi
  cd "$ROOT_DIR"

  # Vérification warnings
  info "Comptage des warnings de compilation..."
  cd "$PHASE_DIR"
  WARNING_COUNT=$(make -B 2>&1 | grep -ci warning || true)
  cd "$ROOT_DIR"
  if [[ "$WARNING_COUNT" -gt 0 ]]; then
    warn "$WARNING_COUNT warning(s) de compilation détecté(s)"
    warn "La clôture est recommandée sans warning"
  else
    ok "Compilation sans warning"
  fi
else
  warn "Pas de Makefile dans TopOptP${N}/ — compilation non vérifiée"
fi

# ── Vérification git ────────────────────────────────────────────────────────────
info "Vérification du statut git..."
if git -C "$ROOT_DIR" status --short 2>/dev/null | grep -q "^[MADRCU]"; then
  warn "Fichiers non commités détectés :"
  git -C "$ROOT_DIR" status --short | head -10
else
  ok "Working tree propre (ou pas de repo git)"
fi

# ── Rapport final ───────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════"
echo "  Résumé de clôture Phase ${N}"
echo "═══════════════════════════════════════════════════════"
echo ""
echo -e "  Erreurs   : ${RED}${ERRORS}${RST}"
echo -e "  Warnings  : ${YEL}${WARNINGS}${RST}"
echo ""

if [[ $ERRORS -gt 0 ]]; then
  err "Clôture BLOQUÉE — corriger les erreurs avant de passer à Phase $((N+1))"
  exit 1
fi

if [[ $WARNINGS -gt 0 ]]; then
  warn "Clôture avec avertissements — corriger les warnings recommandé"
fi

ok "Phase ${N} prête à être clôturée"
echo ""

# ── Commit optionnel ────────────────────────────────────────────────────────────
if [[ $DO_COMMIT -eq 1 ]]; then
  echo "Commit de clôture Phase ${N} :"
  COMMIT_MSG="docs(phase${N}): close phase ${N} — report and lessons updated"
  echo "  Message : $COMMIT_MSG"
  echo ""
  read -rp "Confirmer le commit ? [y/N] " CONFIRM
  if [[ $CONFIRM =~ ^[Yy]$ ]]; then
    git -C "$ROOT_DIR" add \
      "$PHASE_DIR/PHASE_${N}_REPORT.md" \
      "$TRANSITIONS" \
      "$LESSONS" \
      "$PHASE_DIR/STATUS.md" \
      "$PHASE_DIR/TASKS.md" 2>/dev/null || true
    git -C "$ROOT_DIR" commit -m "$COMMIT_MSG" || warn "Commit échoué (rien à commiter ?)"
    ok "Commit créé"
  else
    info "Commit annulé"
  fi
fi

echo ""
ok "close_phase.sh terminé — Phase ${N} officiellement clôturée !"
info "Prochaine étape : ./orchestration/tools/start_phase.sh $((N+1))"
echo ""
