#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analiza los artefactos de una demo/test: valida .exe/.elf/.map, exige los
# simbolos _start/_main y lanza el analizador visual si existe captura.
# Sustituye a analyze-demo.ps1.
#
# Uso: tools/analyze/analyze-demo.sh <demo|test>
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

DEMO="${1:-}"
if [ -z "$DEMO" ]; then
	echo "Uso: tools/analyze/analyze-demo.sh <demo|test>" >&2
	exit 2
fi

DEMO_PATH="$ROOT/$DEMO"
DEMO_NAME="$(basename "$DEMO_PATH")"
OUT_DIR="$ROOT/out/demos/$DEMO_NAME"
EXE="$OUT_DIR/$DEMO_NAME.exe"
ELF="$OUT_DIR/$DEMO_NAME.elf"
MAP="$OUT_DIR/$DEMO_NAME.map"
SCREENSHOT="$ROOT/out/run/$DEMO_NAME/screenshot.png"

[ -f "$EXE" ] || { echo "No existe $EXE. Ejecuta primero tools/build/build-demo.sh." >&2; exit 1; }
[ -f "$ELF" ] || { echo "No existe $ELF." >&2; exit 1; }
[ -f "$MAP" ] || { echo "No existe $MAP." >&2; exit 1; }

# Símbolos esperados en el mapa (el runtime los define).
if ! grep -qE "(_start|_main)" "$MAP"; then
	echo "Faltan simbolos esperados (_start/_main) en $MAP." >&2
	exit 1
fi

EXE_BYTES="$(stat -c %s "$EXE" 2>/dev/null || wc -c <"$EXE")"
ELF_BYTES="$(stat -c %s "$ELF" 2>/dev/null || wc -c <"$ELF")"

cat <<EOF
Demo       : $DEMO_NAME
Executable : $EXE
ExeBytes   : $EXE_BYTES
ElfBytes   : $ELF_BYTES
Map        : $MAP
Screenshot : $([ -f "$SCREENSHOT" ] && echo "$SCREENSHOT" || echo "(sin captura todavia)")
Status     : OK
EOF

if [ -f "$SCREENSHOT" ]; then
	ANALYZER="$DEMO_PATH/analyze-screenshot.sh"
	if [ ! -f "$ANALYZER" ]; then
		ANALYZER="$ROOT/tools/analyze/analyze-screenshot.sh"
	fi
	if ! "$ANALYZER" "$SCREENSHOT"; then
		echo "La captura existe, pero no supera el analisis visual automatico." >&2
		exit 1
	fi
fi

exit 0
