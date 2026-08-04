#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Vision Review: consulta un VLM OpenAI-compatible (p. ej. LM Studio) sobre
# pocos frames seleccionados. Sustituye al antiguo vision-review.ps1.
#
# Uso: tools/vision-review/vision-review.sh [opciones]
#   --review-request <request.json> --provider <provider.json>
#   --source <carpeta|video> --profile <perfil> --out-dir <dir> ...
# Ver docs/testing/VISION_REVIEW_ROADMAP.md.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

exec node "$ROOT/dist/tools/vision-review/vision-review.js" "$@"
