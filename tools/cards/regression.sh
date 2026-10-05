#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Regresion de nivel de los motores de naipes (`eng::cards`).
#
# Compila `tools/cards/selfplay.cpp` y ejecuta un barrido determinista
# (`--compare --sweep`) con semilla fija; despues compara el CSV contra la linea
# base congelada (`tools/cards/regression-baseline.csv`) con
# `tools/cards/check-regression.mjs`. Cualquier cambio en reglas, equity o IA que
# altere el resultado hace fallar el gate.
#
# Uso: tools/cards/regression.sh [--update]
#   --update  regenera la base con el resultado actual (solo si es intencionado).
#
# Se invoca desde `tools/run-host-tests.sh` en la pasada completa.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
BIN="$ROOT/out/tmp/cards-selfplay"
CSV="$ROOT/out/cards/selfplay/regression.csv"

# Runtime del compilador en el PATH + `.exe` de MinGW (ver tools/scripts/host-cxx-env.sh).
. "$ROOT/tools/scripts/host-cxx-env.sh"
host_cxx_prepare "$CXX"

mkdir -p "$ROOT/out/tmp" "$ROOT/out/cards/selfplay"

# Un `cards-selfplay` antiguo (p. ej. un ELF de Linux) haría sombra al binario recién
# compilado; se elimina el homónimo sin extensión y se resuelve el ejecutable real.
rm -f "$BIN"
"$CXX" -std=gnu++23 -I"$ROOT/engine/include" -O2 -Wall -Wextra \
	"$ROOT/tools/cards/selfplay.cpp" -o "$BIN"

UPDATE=""
if [ "${1:-}" = "--update" ]; then
	UPDATE="--update"
fi

"$(host_exe "$BIN")" 200 --seats 4 --compare --sweep --sessions 4 --seed 12345 \
	--table-samples 32 --csv "$CSV" --quiet

if command -v node >/dev/null 2>&1; then
	node "$ROOT/tools/cards/check-regression.mjs" "$CSV" $UPDATE
else
	echo "[cards-regression] node no disponible; se omite la comparacion" >&2
fi
