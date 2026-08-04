#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Comprueba la captura final del test L0-010. La demo restaura el sistema antes
# de terminar, asi que la captura final debe ser el Workbench (no una pantalla
# negra). Sustituye a analyze-screenshot.ps1.
# Uso: analyze-screenshot.sh <imagen.png>
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

IMAGE="${1:-}"
if [ -z "$IMAGE" ] || [ ! -f "$IMAGE" ]; then
	echo "No existe la captura: $IMAGE" >&2
	exit 1
fi

if ! python3 "$ROOT/tools/analyze/assert_no_inner_black.py" "$IMAGE" 0.95; then
	echo "La captura $IMAGE es (casi) toda negra." >&2
	exit 1
fi

echo "OK captura del test L0-010: $IMAGE"
