#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate visual de la demo 215 (widgets de `eng::ui`): captura una secuencia con el
# runner y verifica que la UI se pinta (panel/texto/bisel/foco) y que el slider
# anima. La invoca tools/test-regression.sh (puede recibir --warp; aqui solo se
# reenvia --warp al runner).
#
# Uso: demos/features/ui/amiga/215_gui_widgets/analyze-sequence.sh [--warp] [...]
#
# Interaccion (click real): la demo lee el raton por **deltas** (JOYxDAT) y arranca el
# cursor en (0,0); el boton "Aceptar" esta en (66,49). Para disparar su `on_click` de
# forma determinista (movimiento relativo, no depende de la posicion absoluta del
# emulador):
#
#   bash tools/run/run-demo.sh demos/features/ui/amiga/215_gui_widgets \
#       --sequence-frames 6 --mouse-click-at 66,49
#
# La etiqueta de estado pasa de "Listo. Sin pulsar." a "Pulsado: on_click." (leelo con el
# modelo de vision local). Alternativa equivalente en mitad de la captura:
# `--inject-commands 'input mouse move 66 49|sleep:250|input mouse button 0 1|sleep:160|input mouse button 0 0' --inject-sample 3`.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/features/ui/amiga/215_gui_widgets"

RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-frames 4 --sequence-interval-ms 250)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

# La secuencia vive en out/run/<demo>/<CONFIG_ID>/sequence; el id de build de una feature
# incluye la ruta (p. ej. `ui_amiga_215_gui_widgets`), por eso se busca por el leaf.
SEQ="$(find "$ROOT/out/run" -maxdepth 3 -type d -name sequence -path '*215_gui_widgets*' 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 215" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-gui-widgets.mjs" --sequence-dir "$SEQ"
