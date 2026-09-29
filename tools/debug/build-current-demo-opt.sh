#!/usr/bin/env bash
# Compila la demo o test que contiene el archivo fuente indicado con -O2
# (`--release`, el perfil rapido) y publica una copia estable para el depurador
# integrado de Bartman bajo el nombre current_opt.
#
# Diferencia con build-current-demo.sh: ese usa -O1 (`--debug`, perfil de
# depuracion fiable); este usa -O2 y es el que corre mas rapido en el emulador.
# A cambio, las variables locales pueden estar optimizadas (dificil de depurar).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Invalida SIEMPRE el binario optimizado publicado al EMPEZAR (mismo motivo que
# build-current-demo.sh): si algo falla, no debe quedar un `current_opt` viejo que el
# depurador arranque como si fuera la demo activa. Solo se republica al final, en exito.
CURRENT_OUT="$ROOT/out/debug-current"
mkdir -p "$CURRENT_OUT"
rm -f "$CURRENT_OUT/current_opt" "$CURRENT_OUT/current_opt.exe" "$CURRENT_OUT/current_opt.elf" \
      "$CURRENT_OUT/current_opt.map" "$CURRENT_OUT/current_opt.s"

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
		echo "F5 compila la demo que contiene el archivo ACTIVO. Para ejecutar la demo" >&2
		echo "107_xlimited_corkscrew abre y enfoca su src/main.cpp antes de pulsar F5:" >&2
		echo "  demos/techniques/amiga/playfield/107_xlimited_corkscrew/src/main.cpp" >&2
		CURRENT_OUT="$ROOT/out/debug-current"
		mkdir -p "$CURRENT_OUT"
		cat > "$CURRENT_OUT/session_opt.json" <<EOF
{
  "status": "failed",
  "reason": "file_not_in_demo_src",
  "source": "$SOURCE",
  "builtAt": "$(date -Iseconds)"
}
EOF
		exit 2
		;;
esac

if [ ! -d "$ROOT/$TARGET" ]; then
	echo "No existe el target: $ROOT/$TARGET" >&2
	exit 1
fi

BUILD_SCRIPT="$ROOT/tools/build/build-demo.sh"
# --release => -O2 (perfil rapido; el de depuracion es build-current-demo.sh).
"$BUILD_SCRIPT" "$TARGET" --release --clean

TARGET_NAME="$(basename "$TARGET")"
# Id de build (features por ruta, resto por leaf), igual que build-demo.sh.
TARGET_REL="${TARGET#demos/}"; TARGET_REL="${TARGET_REL#tests/}"
case "$TARGET_REL" in
	features/*) DEMO_ID="$(printf '%s' "${TARGET_REL#features/}" | tr '/' '_')" ;;
	*) DEMO_ID="$TARGET_NAME" ;;
esac
SOURCE_OUT="$ROOT/out/demos/$DEMO_ID"
CURRENT_OUT="$ROOT/out/debug-current"
mkdir -p "$CURRENT_OUT"
# Build por configuraciones: --release = -O2 se publica en .../A500_release/.
CONFIG_DIRS="$(ls -dt "$SOURCE_OUT"/A500_release 2>/dev/null)"
CONFIG_EXE="$(ls -t "$CONFIG_DIRS"/*.exe 2>/dev/null | head -n1)"
if [ -z "$CONFIG_EXE" ]; then
	CONFIG_EXE="$(ls -t "$SOURCE_OUT"/*/*.exe 2>/dev/null | head -n1)"
fi
if [ -z "$CONFIG_EXE" ]; then
	echo "No se encontró el .exe de $TARGET_NAME (build por configuraciones)." >&2
	exit 1
fi
CONFIG_BASE="${CONFIG_EXE%.exe}"
echo "[debug-current] binario: $CONFIG_EXE"
cp "$CONFIG_BASE.exe" "$CURRENT_OUT/current_opt.exe"
if command -v cygpath >/dev/null 2>&1; then
	SOURCE_EXE_WIN="$(cygpath -w "$CONFIG_BASE.exe")"
	CURRENT_NO_EXT_WIN="$(cygpath -w "$CURRENT_OUT/current_opt")"
	powershell.exe -NoProfile -Command "Copy-Item -LiteralPath '$SOURCE_EXE_WIN' -Destination '$CURRENT_NO_EXT_WIN' -Force"
else
	cp "$CONFIG_BASE.exe" "$CURRENT_OUT/current_opt"
fi
cp "$CONFIG_BASE.elf" "$CURRENT_OUT/current_opt.elf"
cp "$CONFIG_BASE.map" "$CURRENT_OUT/current_opt.map"
cp "$CONFIG_BASE.s" "$CURRENT_OUT/current_opt.s"

if command -v cygpath >/dev/null 2>&1; then
	SOURCE_WIN="$(cygpath -w "$SOURCE")"
	TARGET_WIN="$(cygpath -w "$ROOT/$TARGET")"
	SCRIPT_WIN="$(cygpath -w "${BASH_SOURCE[0]}")"
	powershell.exe -NoProfile -Command "Set-ItemProperty -LiteralPath '$SOURCE_WIN' -Name LastWriteTime -Value (Get-Date)"
fi

echo "Optimizado listo: $CURRENT_OUT/current_opt.exe (de $TARGET)"
