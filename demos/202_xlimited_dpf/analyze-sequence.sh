#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# analyze-sequence.sh — Demo 202 (DPF 3+3): build + run + verificación.
#   bash demos/202_xlimited_dpf/analyze-sequence.sh [--warp] [--config A500_debug]
# Ejecuta la demo, captura una secuencia y verifica que el parallax 2:1 es real
# (BG del mapa se mueve ~2× el FG de plaquettes) con verify-parallax.mjs.
# ---------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
CONFIG=A500_debug
WARP=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --warp) WARP=(--warp) ;;
    --config) CONFIG="$2"; shift ;;
    --release) CONFIG=A500_release ;;
    *) echo "arg desconocido: $1" >&2; exit 2 ;;
  esac
  shift
done

echo "[202] build (${CONFIG})..."
bash ./tools/build/build-demo.sh demos/202_xlimited_dpf "--$([ "$CONFIG" = A500_release ] && echo release || echo debug)" --clean >/dev/null

echo "[202] run + secuencia..."
bash ./tools/run/run-demo.sh demos/202_xlimited_dpf --config "$CONFIG" --sequence-frames 3 --sequence-interval-ms 150 "${WARP[@]}" >/dev/null

echo "[202] verificación parallax 2:1..."
node ./demos/202_xlimited_dpf/verify-parallax.mjs --config "$CONFIG"
