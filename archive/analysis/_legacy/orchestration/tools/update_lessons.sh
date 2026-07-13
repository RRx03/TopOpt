#!/usr/bin/env bash
# update_lessons.sh — Ouvrir LESSONS_LEARNED.md dans $EDITOR
# Usage : ./orchestration/tools/update_lessons.sh
#
# Ouvre le fichier et rappelle le format d'entrée.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
LESSONS="$ROOT_DIR/orchestration/LESSONS_LEARNED.md"

if [[ ! -f "$LESSONS" ]]; then
  echo "ERREUR : $LESSONS introuvable" >&2
  exit 1
fi

# Déterminer le prochain numéro LL disponible
LAST_LL=$(grep -oE 'LL-[0-9]+' "$LESSONS" | grep -v 'LIT' | grep -oE '[0-9]+' | sort -n | tail -1 || echo "0")
NEXT_LL=$(printf "%03d" $((10#$LAST_LL + 1)))

echo ""
echo "LESSONS_LEARNED.md — Prochain numéro disponible : LL-${NEXT_LL}"
echo ""
echo "Format d'une nouvelle entrée :"
echo ""
echo "  ### LL-${NEXT_LL} : [Titre] (Phase N, $(date +%Y-%m-%d))"
echo "  - **Symptôme** : ce qui s'est passé"
echo "  - **Cause** : pourquoi"
echo "  - **Conséquence** : impact si non traité"
echo "  - **Leçon** : règle à retenir"
echo "  - **Vérification** : comment s'assurer que le piège est évité"
echo ""
echo "Ouverture dans \${EDITOR:-nano}..."
echo ""

# Petit délai pour lire le format
sleep 1

# Ouvrir dans $EDITOR (ou nano par défaut)
${EDITOR:-nano} "$LESSONS"
