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
SCREENSHOT="$ROOT/out/run/$DEMO_NAME/screenshot.png"

# El build nombra el exe con CONFIG_ID (MACHINE_flags_modo): out/demos/<demo>/<CONFIG_ID>/<demo>.<CONFIG_ID>.exe.
# Localizamos la config: prioridad por defecto (sin flags, _debug), luego otras _debug,
# luego release; desempate por mtime. Si no hay config, ruta vieja (pre-CONFIG_ID).
pick_config() {
	local cfg best_rank=99 best_mt=0 best=""
	for cfg in "$OUT_DIR"/*/; do
		[ -d "$cfg" ] || continue
		local name; name="$(basename "$cfg")"
		[ -f "$cfg/$DEMO_NAME.$name.exe" ] || continue
		local rank
		if [[ "$name" =~ ^[^_]+_(debug|o0)$ ]]; then rank=0
		elif [[ "$name" =~ _debug$ ]]; then rank=1
		elif [[ "$name" =~ ^[^_]+_release$ ]]; then rank=2
		else rank=3; fi
		local mt; mt="$(stat -c %Y "$cfg/$DEMO_NAME.$name.exe" 2>/dev/null || echo 0)"
		if [ "$rank" -lt "$best_rank" ] || { [ "$rank" -eq "$best_rank" ] && [ "$mt" -gt "$best_mt" ]; }; then
			best_rank=$rank; best_mt=$mt; best="$name"
		fi
	done
	echo "$best"
}

CONFIG_ID="$(pick_config)"
if [ -n "$CONFIG_ID" ]; then
	EXE="$OUT_DIR/$CONFIG_ID/$DEMO_NAME.$CONFIG_ID.exe"
	ELF="$OUT_DIR/$CONFIG_ID/$DEMO_NAME.$CONFIG_ID.elf"
	MAP="$OUT_DIR/$CONFIG_ID/$DEMO_NAME.$CONFIG_ID.map"
else
	EXE="$OUT_DIR/$DEMO_NAME.exe"
	ELF="$OUT_DIR/$DEMO_NAME.elf"
	MAP="$OUT_DIR/$DEMO_NAME.map"
fi

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
