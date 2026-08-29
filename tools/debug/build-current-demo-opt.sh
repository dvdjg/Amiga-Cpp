#!/usr/bin/env bash
# Compila la demo o test que contiene el archivo fuente indicado con -O1 (la
# optimizacion estandar de las demos) y publica una copia estable para el
# depurador integrado de Bartman bajo el nombre current_opt.
#
# Diferencia con build-current-demo.sh: este NO pasa --o0, asi que el codigo
# generado es ~2x mas rapido y las demos alcanzan 50fps en el emulador. A
# cambio, las variables locales pueden estar optimizadas (dificil de depurar).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SOURCE_INPUT="${1:-}"
if [ -z "$SOURCE_INPUT" ]; then
	echo "Uso: tools/debug/build-current-demo-opt.sh <archivo-fuente>" >&2
	exit 2
fi

if command -v cygpath >/dev/null 2>&1 && [[ "$SOURCE_INPUT" =~ ^[A-Za-z]:[\\/] ]]; then
	SOURCE_INPUT="$(cygpath -u "$SOURCE_INPUT")"
fi
SOURCE="$(realpath "$SOURCE_INPUT")"
RELATIVE="${SOURCE#"$ROOT/"}"
if [ "$RELATIVE" = "$SOURCE" ]; then
	echo "El archivo debe estar dentro de $ROOT: $SOURCE" >&2
	exit 1
fi

case "$RELATIVE" in
	demos/*/src/*|tests/*/src/*)
		TARGET="${RELATIVE%%/src/*}"
		;;
	*)
		echo "El archivo no pertenece a demos/<nombre>/src o tests/<nombre>/src: $RELATIVE" >&2
		exit 1
		;;
esac

if [ ! -d "$ROOT/$TARGET" ]; then
	echo "No existe el target: $ROOT/$TARGET" >&2
	exit 1
fi

BUILD_SCRIPT="$ROOT/tools/build/build-demo.sh"
# --debug sin --o0 => -O1 (la optimizacion estandar de las demos).
"$BUILD_SCRIPT" "$TARGET" --debug --clean

TARGET_NAME="$(basename "$TARGET")"
SOURCE_OUT="$ROOT/out/demos/$TARGET_NAME"
CURRENT_OUT="$ROOT/out/debug-current"
mkdir -p "$CURRENT_OUT"
cp "$SOURCE_OUT/$TARGET_NAME.exe" "$CURRENT_OUT/current_opt.exe"
if command -v cygpath >/dev/null 2>&1; then
	SOURCE_EXE_WIN="$(cygpath -w "$SOURCE_OUT/$TARGET_NAME.exe")"
	CURRENT_NO_EXT_WIN="$(cygpath -w "$CURRENT_OUT/current_opt")"
	powershell.exe -NoProfile -Command "Copy-Item -LiteralPath '$SOURCE_EXE_WIN' -Destination '$CURRENT_NO_EXT_WIN' -Force"
else
	cp "$SOURCE_OUT/$TARGET_NAME.exe" "$CURRENT_OUT/current_opt"
fi
cp "$SOURCE_OUT/$TARGET_NAME.elf" "$CURRENT_OUT/current_opt.elf"
cp "$SOURCE_OUT/$TARGET_NAME.map" "$CURRENT_OUT/current_opt.map"
cp "$SOURCE_OUT/$TARGET_NAME.s" "$CURRENT_OUT/current_opt.s"

if command -v cygpath >/dev/null 2>&1; then
	SOURCE_WIN="$(cygpath -w "$SOURCE")"
	TARGET_WIN="$(cygpath -w "$ROOT/$TARGET")"
	SCRIPT_WIN="$(cygpath -w "${BASH_SOURCE[0]}")"
	powershell.exe -NoProfile -Command "Set-ItemProperty -LiteralPath '$SOURCE_WIN' -Name LastWriteTime -Value (Get-Date)"
fi

echo "Optimizado listo: $CURRENT_OUT/current_opt.exe (de $TARGET)"
