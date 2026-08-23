#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# profile-analyze.sh — Pipeline completo: captura perfil -> extrae frames ->
# analiza con Ollama local. Deja evidencia en out/profile/<nombre>.
#
# Uso:
#   tools/profile/profile-analyze.sh <outName> [frames] \
#       [--model MODEL] [--contact-sheet] [--wait-cmd 'cmd' --contains 'txt']
#
# Ejemplo:
#   tools/profile/profile-analyze.sh demo050 6 --model qwen2.5-vl:7b \
#       --wait-cmd 'runstatus 0x<addr>' --contains 'READY'
# ---------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
NAME="${1:?Uso: profile-analyze.sh <outName> [frames] [opciones]}"
FRAMES="${2:-1}"
shift 2 2>/dev/null || shift 1

OUT="$ROOT/out/profile/$NAME"
mkdir -p "$OUT"
BIN="$OUT/$NAME.profile.bin"

echo "[profile-analyze] capturando $FRAMES frame(s) a $BIN"
node "$ROOT/tools/profile/capture-profile.mjs" "$BIN" "$FRAMES" "$@"

echo "[profile-analyze] extrayendo frames"
node "$ROOT/tools/profile/profile-extract.mjs" "$BIN" "$OUT"

echo "[profile-analyze] analizando con Ollama"
node "$ROOT/tools/profile/ollama-analyze.mjs" "$OUT" "$@"

echo "[profile-analyze] OK: $OUT (frames + profile-summary.json + ollama-report.md)"
