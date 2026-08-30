#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# probe-screen.sh — Sondeo visual de una demo: lanza via runner (que conecta
# GDB, alcanza READY y deja la demo renderizando), espera READY, captura un
# perfil por el canal lateral, extrae frames y analiza con Ollama local.
#
# Uso:
#   tools/profile/probe-screen.sh <demo> [frames] [opciones de ollama-analyze]
#
# Ejemplo:
#   tools/profile/probe-screen.sh demos/050_blitter_bobs 4 \
#       --model qwen3-vl:8b-instruct-q8_0 \
#       --prompt-file tools/profile/prompts/050-blitter-bobs.md
# ---------------------------------------------------------------------------
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO="${1:?Uso: probe-screen.sh <demo> [frames] [opciones]}"
FRAMES="${2:-4}"
shift 2 2>/dev/null || shift 1

NAME="$(basename "$DEMO")"
OUT="$ROOT/out/profile/$NAME"
mkdir -p "$OUT"
BIN="$OUT/$NAME.profile.bin"
RUNLOG="$OUT/run-demo.log"

# 1. Lanza la demo via runner en segundo plano (WinUAE persistente).
export WINUAE_GDB_PERSIST_LISTENER=1
"$ROOT/tools/run/run-demo.sh" "$DEMO" --keep-running > "$RUNLOG" 2>&1 &
RUNNER=$!
cleanup() { kill "$RUNNER" 2>/dev/null || true; }
trap cleanup EXIT

# 2. Espera a que la demo alcance READY (lo publica el runner en su log).
echo "[probe-screen] esperando READY de $DEMO..."
ready=0
for i in $(seq 1 90); do
	if grep -q 'side-channel READY' "$RUNLOG" 2>/dev/null; then
		echo "[probe-screen] demo READY (tras ~${i}s)"
		ready=1
		break
	fi
	if ! kill -0 "$RUNNER" 2>/dev/null && grep -qi 'error\|fallo' "$RUNLOG" 2>/dev/null; then
		echo "[probe-screen] el runner fallo; ver $RUNLOG" >&2
		exit 1
	fi
	sleep 1
done
[ "$ready" = "1" ] || { echo "[probe-screen] timeout esperando READY; ver $RUNLOG" >&2; exit 1; }

# 3. Asentamiento corto para que el frame ya este en pantalla.
SETTLE_MS="${SETTLE_MS:-2000}"
sleep $((SETTLE_MS / 1000))

# 4. Captura el perfil.
echo "[probe-screen] capturando $FRAMES frame(s)"
node "$ROOT/tools/profile/capture-profile.mjs" "$BIN" "$FRAMES" --lock-owner probe-screen || {
  echo "[probe-screen] fallo la captura del perfil" >&2
  exit 1
}

# 5. Extrae frames.
echo "[probe-screen] extrayendo frames"
node "$ROOT/tools/profile/profile-extract.mjs" "$BIN" "$OUT" || exit 1

# 6. Analiza con Ollama (meta + montaje por defecto).
echo "[probe-screen] analizando con Ollama"
node "$ROOT/tools/profile/ollama-analyze.mjs" "$OUT" "$@" || exit 1

echo "[probe-screen] OK: $OUT (bin + frames + profile-summary.json + ollama-report.md)"
