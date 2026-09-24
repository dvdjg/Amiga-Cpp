#!/usr/bin/env bash
# Escucha un modulo OctaMED en hardware/emulador (sin --warp, para oirlo a velocidad real).
#
# Uso:  bash tools/audio/listen-octamed.sh <n> [segundos]
#   n=0 octamed_test · 1 mammagamma · 2 mammagamma_SPD · 3 playroutine_test
#
# Deja el emulador VIVO (`--keep-running`) para que suene; cerrar WinUAE a mano al terminar.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

N="${1:-1}"
SECS="${2:-20}"

export EXTRA_DEFINES="-DENG_AUDIO_OCTAMED -DMED_MODULE_NUM=${N} -DOCTAMED_READY_FRAME=0"
echo "[listen] build MED_MODULE_NUM=${N}"
bash tools/build/build-demo.sh demos/techniques/amiga/audio/274_octamed_probe --debug 2>&1 | grep -iE 'error|OK    ' | tail -1

# El runner prioriza A500_debug sobre la build de flags: hay que forzarla.
CFG="$(ls -dt out/demos/274_octamed_probe/A500_eng_audio_octamed* 2>/dev/null | head -1 | xargs basename)"
echo "[listen] config=${CFG}; sonando ~${SECS}s (SIN warp)"
WINUAE_SIDE_CHANNEL_PORT=2421 bash tools/run/run-demo.sh demos/techniques/amiga/audio/274_octamed_probe \
    --config "$CFG" --keep-running --allow-timeout-fallback --wait-ms "$((SECS * 1000))" 2>&1 \
    | grep -iE 'READY|FAILED|timeout|fallback|screenshot|ok$' | tail -4
echo "[listen] (el emulador sigue abierto; cierralo cuando quieras)"
