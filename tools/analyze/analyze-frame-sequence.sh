#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analiza una secuencia de frames (huellas, diff por pares, hoja de contacto).
# Sustituye a analyze-frame-sequence.ps1. Delega en analyze_frame_sequence.py.
# Uso: tools/analyze/analyze-frame-sequence.sh <carpeta> [opciones]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 "$ROOT/tools/analyze/analyze_frame_sequence.py" "$@"
