#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Barrido de SALUD en runtime de las demos de una plataforma.
#
# Lanza cada demo con el runner (warp) y clasifica:
#   OK              -> alcanza READY por el canal lateral.
#   FAILED:<detalle>-> la demo publica FAILED (detail = codigo de fallo).
#   TIMEOUT         -> no publica READY ni FAILED (cuelgue/guru): NO VERIFICADA.
#   NOEXE           -> falta el .exe (demo con assets generados sin construir).
#
# Complementa a `tools/build/build-all-demos.sh` (que solo mira el build): un
# 0 FAIL de compilacion NO significa que las demos arranquen.
#
# Uso:
#   bash tools/analyze/sweep-demo-health.sh [patron1 patron2 ...]
#   ENG_READY_MS=30000 bash tools/analyze/sweep-demo-health.sh 107 201
# Sin patrones recorre todas. Detalle por demo en out/tmp/sweep-demos.log.
# ---------------------------------------------------------------------------
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT" || exit 1

LOG="$ROOT/out/tmp/sweep-demos.log"
TO="${ENG_READY_MS:-30000}"
: > "$LOG"

match() {
	local n="$1"; shift
	[ "$#" -eq 0 ] && return 0
	for p in "$@"; do [[ "$n" == *"$p"* ]] && return 0; done
	return 1
}

for d in demos/amiga/*/; do
	name="$(basename "$d")"
	[ -f "$d/src/main.cpp" ] || continue
	match "$name" "$@" || continue
	out="$(bash ./tools/run/run-demo.sh "$d" --warp --side-channel-timeout-ms "$TO" 2>&1)"
	st="TIMEOUT"
	if echo "$out" | grep -q "\[run-demo\] ok"; then
		st="OK"
	elif echo "$out" | grep -q "FAILED"; then
		st="FAILED:$(echo "$out" | grep -o 'detail=[0-9]*' | head -1)"
	elif echo "$out" | grep -qi "No existe.*\.exe"; then
		st="NOEXE"
	fi
	printf '%-26s %s\n' "$st" "$name" | tee -a "$LOG"
done

echo "--- resumen (timeout ${TO} ms) ---"
for k in OK FAILED TIMEOUT NOEXE; do printf '%s=%s\n' "$k" "$(grep -c "^$k" "$LOG")"; done
