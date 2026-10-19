#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate de la demo 305 (overlay sobre juego en movimiento, GameRedraws + Backing,
# skins y degradacion por presupuesto).
#
# Doble evidencia (leccion DT-035):
#   1. **Memoria** (contadores): `fps10` (cadencia), `ov_bad`/`bk_bad` (ops y
#      restauraciones del backing fallidas; deben ser 0), `deg_changes` (la
#      escalera de degradacion actua) y `blit_bad` (blits del juego).
#   2. **Visual** (secuencia por intervalos, CPU viva): presencia del panel y
#      movimiento del campo/actor; los chequeos de animacion son informativos
#      (el video puede congelarse: DT-023/035).
#
# Uso: demos/features/ui/amiga/305_hud_overlay/analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/features/ui/amiga/305_hud_overlay"

# --- 1) Evidencia de memoria -------------------------------------------------
COUNTERS_OUT="$(bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --settle-ms 12000 \
	--read-debugperiph counters 2>&1)"
echo "$COUNTERS_OUT" | grep -E 'name="(vbl|frame|ov_bad|bk_bad|deg_changes|blit_bad|ov_ok)"' || true
val() { echo "$COUNTERS_OUT" | grep "name=\"$1\"" | grep -o 'value=[0-9]*' | cut -d= -f2; }
VBL="$(val vbl)"; FRAME="$(val frame)"; OV_BAD="$(val ov_bad)"; BK_BAD="$(val bk_bad)"
DEG="$(val deg_changes)"; BLIT_BAD="$(val blit_bad)"; OV_OK="$(val ov_ok)"
VBL="${VBL:-0}"; FRAME="${FRAME:-0}"; OV_BAD="${OV_BAD:-0}"; BK_BAD="${BK_BAD:-0}"
DEG="${DEG:-0}"; BLIT_BAD="${BLIT_BAD:-0}"; OV_OK="${OV_OK:-0}"
if [ "$VBL" -lt 400 ]; then
	echo "FAIL: el latido de VBlank no avanzo (vbl=$VBL; app muerta o ventana corta)" >&2
	exit 1
fi
# Cadencia honesta (DT-036): ratio frame/vbl en mil. La demo es pesada (scroll +
# paneles + backing) y su regimen medido es ~0,70; el umbral exige >= 0,60.
RATIO=$((FRAME * 1000 / VBL))
if [ "$RATIO" -lt 600 ]; then
	echo "FAIL: cadencia baja (frame=$FRAME / vbl=$VBL = $RATIO/1000 < 600)" >&2
	exit 1
fi
if [ "$OV_BAD" -ne 0 ] || [ "$BK_BAD" -ne 0 ] || [ "$BLIT_BAD" -ne 0 ]; then
	echo "FAIL: fallos (ov_bad=$OV_BAD, bk_bad=$BK_BAD, blit_bad=$BLIT_BAD)" >&2
	exit 1
fi
if [ "$OV_OK" -lt 1 ]; then
	echo "FAIL: sin ops aplicadas (ov_ok=$OV_OK)" >&2
	exit 1
fi
if [ "$DEG" -lt 2 ]; then
	echo "FAIL: la escalera de degradacion no actua (deg_changes=$DEG < 2)" >&2
	exit 1
fi
echo "[analyze] memoria OK: vbl=$VBL, frame=$FRAME (ratio $RATIO/1000), ov_ok=$OV_OK, ov_bad=$OV_BAD, bk_bad=$BK_BAD, blit_bad=$BLIT_BAD, deg_changes=$DEG"

# --- 2) Evidencia visual: secuencia por intervalos ---------------------------
RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-frames 8 --sequence-interval-ms 900 \
	--settle-ms 2500)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

SEQ="$(find "$ROOT/out/run" -maxdepth 3 -type d -name sequence -path '*305_hud_overlay*' 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 305" >&2
	exit 1
fi
node "$ROOT/tools/analyze/verify-hud-overlay.mjs" --sequence-dir "$SEQ"
