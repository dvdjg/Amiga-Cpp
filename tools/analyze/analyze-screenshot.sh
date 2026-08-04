#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Comprueba una captura de pantalla de una demo (overlay de depuracion).
# Sustituye a analyze-screenshot.ps1. Delega en analyze_screenshot.py (Pillow).
# Uso: tools/analyze/analyze-screenshot.sh <imagen.png> [minWidth] [minHeight]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 "$ROOT/tools/analyze/analyze_screenshot.py" "$@"
