#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# probe-screen.sh — Sondeo visual de una demo: prepara, lanza WinUAE desacoplado,
# espera READY, captura perfil, extrae frames y analiza con Ollama local.
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
EXT="$(ls -d "$HOME"/.vscode/extensions/bartmanabyss.amiga-debug-* "$HOME"/.cursor/extensions/bartmanabyss.amiga-debug-* 2>/dev/null | sort | tail -1)"
[ -n "$EXT" ] || { echo "No se encontro la extension amiga-debug" >&2; exit 1; }
BIN_DIR="$EXT/bin/win32"
UAE="$BIN_DIR/default.uae"

# 1. Prepara la demo (escribe config + dh1 staged con a.exe) y la deja lista.
"$ROOT/tools/run/run-demo.sh" "$DEMO" --skip-analyze > "$OUT/run-demo-prep.log" 2>&1 || true

# 2. Asegura el trigger y el dh1 correctos en la config.
EXE_NAME="${NAME}.exe"
STAGE="$ROOT/out/run/$NAME/dh1"
[ -d "$STAGE" ] || STAGE="$ROOT/out/demos/$NAME"
sed -i "s/^debugging_trigger=.*$/debugging_trigger=:${EXE_NAME}/" "$UAE" 2>/dev/null || true
# La demo staged como a.exe; ajustamos el trigger a a.exe si existe.
if [ -f "$STAGE/a.exe" ]; then
	sed -i "s/^debugging_trigger=.*$/debugging_trigger=:a.exe/" "$UAE" 2>/dev/null || true
fi

# 3. Lanza WinUAE desacoplado (sobrevive al script).
export WINUAE_GDB_PERSIST_LISTENER=1
(cd "$BIN_DIR" && nohup ./winuae-gdb.exe -portable -norawinput_mouse > "$OUT/winuae.log" 2>&1 &)

# 4. Espera a que el canal lateral responda.
for i in $(seq 1 60); do
	if node "$ROOT/tools/debug/winuae-side-channel.js" hello >/dev/null 2>&1; then
		echo "[probe-screen] WinUAE listo (canal lateral ok)"
		break
	fi
	sleep 1
done

# 5. Captura el perfil (espera opcional por condicion via $@ si incluye --wait-cmd).
echo "[probe-screen] capturando $FRAMES frame(s)"
node "$ROOT/tools/profile/capture-profile.mjs" "$BIN" "$FRAMES" --lock-owner probe-screen

# 6. Extrae frames.
echo "[probe-screen] extrayendo frames"
node "$ROOT/tools/profile/profile-extract.mjs" "$BIN" "$OUT"

# 7. Analiza con Ollama (meta + montaje por defecto).
echo "[probe-screen] analizando con Ollama"
node "$ROOT/tools/profile/ollama-analyze.mjs" "$OUT" "$@"

echo "[probe-screen] OK: $OUT (bin + frames + profile-summary.json + ollama-report.md)"
