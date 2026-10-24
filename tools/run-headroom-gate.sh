#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate opt-in de margen de CPU de la demo 202.
#
# Compila la variante K_202_GAME_LOAD=1 (24 actores de logica, sprite DMA y un
# canal de audio), prepara su runner y exige una ventana estable de 12 s. No se
# ejecuta en run-host-tests (necesita WinUAE); pensado para CI periodica o para
# ejecutarlo a mano antes de tocar el presupuesto del frame.
#
# Uso: bash ./tools/run-headroom-gate.sh [--threshold 49.5] [--warn-only]
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEMO="202_xlimited_dpf"
CONFIG="A500_k_202_game_load1_release"
THRESHOLD="49.5"
WARN_ONLY=0

while [ "$#" -gt 0 ]; do
	case "$1" in
		--threshold) THRESHOLD="$2"; shift 2 ;;
		--warn-only) WARN_ONLY=1; shift ;;
		-h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "Argumento desconocido: $1" >&2; exit 2 ;;
	esac
done

if ! command -v node >/dev/null 2>&1; then
	echo "[headroom] node no disponible." >&2
	exit 1
fi

cd "$ROOT" || exit 1
OUT_DIR="$ROOT/out/headroom"
mkdir -p "$OUT_DIR"
REPORT="$OUT_DIR/${DEMO}_${CONFIG}.json"

echo "[headroom] build $DEMO ($CONFIG)"
EXTRA_DEFINES="-DK_202_GAME_LOAD=1" bash ./tools/build/build-demo.sh \
	demos/techniques/amiga/playfield/$DEMO --release || exit 1

if [ ! -f "out/run/$DEMO/$CONFIG/runner.uae" ]; then
	echo "[headroom] preparando runner"
	bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/$DEMO \
		--config "$CONFIG" --wait-ms 1000 >/dev/null 2>&1 || exit 1
fi

echo "[headroom] midiendo 12 s"
RESULT="$(WINUAE_GDB_PORT="${WINUAE_GDB_PORT:-2347}" \
	WINUAE_SIDE_CHANNEL_PORT="${WINUAE_SIDE_CHANNEL_PORT:-2348}" \
	node ./tools/debug/measure-fps.mjs "$DEMO" "$CONFIG" \
	--duration-ms 12000 --json 2>&1 | grep '^{' | tail -1)"
printf '%s\n' "$RESULT" > "$REPORT"

node - "$RESULT" "$THRESHOLD" <<'NODE'
const raw = process.argv[2];
const threshold = Number(process.argv[3]);
let r;
try { r = JSON.parse(raw); } catch (e) {
  console.error('[headroom] no se pudo leer el JSON de measure-fps');
  process.exit(2);
}
const ok = r.stable === true && r.emulatedFps >= threshold;
console.log(`[headroom] fps=${r.emulatedFps} campos/frame=${r.fieldsPerFrame} estable=${r.stable} umbral=${threshold} -> ${ok ? 'OK' : 'FALLO'}`);
process.exit(ok ? 0 : 1);
NODE
status="$?"
if [ "$status" -ne 0 ] && [ "$WARN_ONLY" -eq 1 ]; then
	echo "[headroom] fallo convertido a aviso (--warn-only)."
	status=0
fi
exit "$status"
