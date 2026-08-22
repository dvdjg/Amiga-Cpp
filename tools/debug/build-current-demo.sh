#!/usr/bin/env bash
# Compila la demo o test que contiene el archivo fuente indicado y publica una
# copia estable para el depurador integrado de Bartman.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SOURCE_INPUT="${1:-}"
if [ -z "$SOURCE_INPUT" ]; then
	echo "Uso: tools/debug/build-current-demo.sh <archivo-fuente>" >&2
	exit 2
fi

# VS Code entrega rutas Windows; Git Bash necesita su forma POSIX.
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
"$BUILD_SCRIPT" "$TARGET" --debug --clean

TARGET_NAME="$(basename "$TARGET")"
SOURCE_OUT="$ROOT/out/demos/$TARGET_NAME"
CURRENT_OUT="$ROOT/out/debug-current"
mkdir -p "$CURRENT_OUT"
cp "$SOURCE_OUT/$TARGET_NAME.exe" "$CURRENT_OUT/current.exe"
if command -v cygpath >/dev/null 2>&1; then
	SOURCE_EXE_WIN="$(cygpath -w "$SOURCE_OUT/$TARGET_NAME.exe")"
	CURRENT_NO_EXT_WIN="$(cygpath -w "$CURRENT_OUT/current")"
	powershell.exe -NoProfile -Command "Copy-Item -LiteralPath '$SOURCE_EXE_WIN' -Destination '$CURRENT_NO_EXT_WIN' -Force"
else
	cp "$SOURCE_OUT/$TARGET_NAME.exe" "$CURRENT_OUT/current"
fi
cp "$SOURCE_OUT/$TARGET_NAME.elf" "$CURRENT_OUT/current.elf"
cp "$SOURCE_OUT/$TARGET_NAME.map" "$CURRENT_OUT/current.map"
cp "$SOURCE_OUT/$TARGET_NAME.s" "$CURRENT_OUT/current.s"

if command -v cygpath >/dev/null 2>&1; then
	SOURCE_WIN="$(cygpath -w "$SOURCE")"
	TARGET_WIN="$(cygpath -w "$ROOT/$TARGET")"
else
	SOURCE_WIN="$SOURCE"
	TARGET_WIN="$ROOT/$TARGET"
fi
cat > "$CURRENT_OUT/session.json" <<EOF
{
  "status": "built",
  "source": "${SOURCE_WIN//\\/\\\\}",
  "target": "${TARGET_WIN//\\/\\\\}",
  "targetName": "$TARGET_NAME",
  "executable": "out/debug-current/current",
  "elf": "out/debug-current/current.elf",
  "map": "out/debug-current/current.map",
  "builtAt": "$(date -Iseconds)",
  "debugger": "BartmanAbyss amiga-debug",
  "machine": "A500",
  "gdbPort": 2345,
  "sideChannelPort": 2346
}
EOF

echo "[debug-current] $TARGET"
echo "[debug-current] source: $SOURCE"
echo "[debug-current] executable: $CURRENT_OUT/current"
echo "[debug-current] session: $CURRENT_OUT/session.json"
