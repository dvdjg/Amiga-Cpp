#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate determinista de la captura de 119_proc_world: el mundo debe traer hierba
# (verde), agua (azul) y masa de terreno variada (nonblue alto), además de la
# franja de versión (texto claro sobre negro).
# ---------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DIR="$ROOT/dist/tools/analyze"
[ -d "$DIR" ] || DIR="$ROOT/tools/analyze"

node "$DIR/analyze_demo_screenshot.js" \
	--demo 119_proc_world \
	--image "$1" \
	--min-white 200 \
	--min-dark 5000 \
	--min-nonblue 20000 \
	--need-green
