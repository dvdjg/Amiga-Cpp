#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# FrameScope: analisis visual temporal determinista de secuencias/videos.
# Sustituye a frame-scope.ps1. Delega en frame_scope.js (Pillow + ffmpeg).
# Uso: tools/framescope/frame-scope.sh --source <carpeta|video> [opciones]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec node "$ROOT/dist/tools/framescope/frame_scope.js" "$@"
