#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate determinista de la captura de 121_proc_corner_layers: el mundo debe traer
# hierba (verde), agua (azul) y masa de terreno variada (nonblue alto), además de
# la franja de versión (texto claro sobre negro) y el actor (masa blanca).
# ---------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DIR="$ROOT/dist/tools/analyze"
[ -d "$DIR" ] || DIR="$ROOT/tools/analyze"

node "$DIR/analyze_demo_screenshot.js" \
	--demo 121_proc_corner_layers \
	--image "$1" \
	--min-white 20 \
	--min-dark 3000 \
	--min-nonblue 20000 \
	--need-green
