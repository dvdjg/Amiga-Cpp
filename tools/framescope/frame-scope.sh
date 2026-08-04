#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# FrameScope: analisis visual temporal determinista de secuencias/videos.
# Sustituye a frame-scope.ps1. Delega en frame_scope.py (Pillow + ffmpeg).
# Uso: tools/framescope/frame-scope.sh --source <carpeta|video> [opciones]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 "$ROOT/tools/framescope/frame_scope.py" "$@"
