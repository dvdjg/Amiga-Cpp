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

# Demos bajo la estructura actual (`demos/techniques/<familia>/<categoria>/<demo>/` y
# `demos/features/<feature>/<plataforma>/<demo>/`): localiza los `src/main.cpp`.
while IFS= read -r d; do
	name="$(basename "$d")"
	match "$name" "$@" || continue
	# Args por demo (`run.args`): mismas reglas que `test-regression.sh`.
	args=(--warp --side-channel-timeout-ms "$TO" --reset-emulator)
	if [ -f "$d/run.args" ]; then
		while IFS= read -r _a; do
			[ -z "$_a" ] && continue
			case "$_a" in \#*) continue ;; esac
			read -r -a _w <<< "$_a"
			args+=("${_w[@]}")
		done < "$d/run.args"
	fi
	out="$(bash ./tools/run/run-demo.sh "$d" "${args[@]}" 2>&1)"
	st="TIMEOUT"
	if echo "$out" | grep -q "\[run-demo\] ok"; then
		st="OK"
	elif echo "$out" | grep -q "FAILED"; then
		st="FAILED:$(echo "$out" | grep -o 'detail=[0-9]*' | head -1)"
	elif echo "$out" | grep -qi "No existe.*\.exe"; then
		st="NOEXE"
	fi
	printf '%-26s %s\n' "$st" "$name" | tee -a "$LOG"
done < <(find demos -path '*/src/main.cpp' | sed 's|/src/main.cpp$||' | sort)

echo "--- resumen (timeout ${TO} ms) ---"
for k in OK FAILED TIMEOUT NOEXE; do printf '%s=%s\n' "$k" "$(grep -c "^$k" "$LOG")"; done
