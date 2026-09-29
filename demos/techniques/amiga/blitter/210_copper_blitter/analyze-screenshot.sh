#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Comprueba la captura de la demo 210: rayas diagonales blancas sobre fondo azul
# oscuro, SIN overlay de depuracion (en modo takeover el overlay tapa el display).
# Valida tambien que el run-report.json corresponde a la demo y llego a Ready.
# Uso: analyze-screenshot.sh <imagen.png>
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"

exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" \
	--image "$1" --demo "210_copper_blitter" \
	--min-white 10 --min-dark 10 --min-nonblue 100
