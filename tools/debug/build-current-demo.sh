#!/usr/bin/env bash
# Compila la demo o test que contiene el archivo fuente indicado y publica una
# copia estable para el depurador integrado de Bartman.
#
# IMPORTANTE (2026-09-01): compila con `--debug` (-O1, config A500_debug), NO
# con `--o0`: el binario -O0 (A500_o0) no arranca en el entorno y, con el
# fallback cruzado antiguo, F5 publicaba/copiaba un .exe de OTRA config o demo
# (por eso se veía 'versión antigua'). El binario se copia SIEMPRE desde el
# config A500_debug y se loguea la ruta exacta en `[debug-current] binario:`.
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
		echo "F5 compila la demo que contiene el archivo ACTIVO. Para ejecutar la demo" >&2
		echo "107_xlimited_corkscrew abre y enfoca su src/main.cpp antes de pulsar F5:" >&2
		echo "  demos/107_xlimited_corkscrew/src/main.cpp" >&2
		echo "(los headers del engine y de otras demos compilan OTRA demo o nada)." >&2
		# Marcar la sesión como no válida para que ninguna herramienta asuma que
		# out/debug-current/current es el demo esperado (evita 'churro' por stale).
		CURRENT_OUT="$ROOT/out/debug-current"
		mkdir -p "$CURRENT_OUT"
		cat > "$CURRENT_OUT/session.json" <<EOF
{
  "status": "failed",
  "reason": "file_not_in_demo_src",
  "source": "$SOURCE",
  "error": "El archivo abierto no es demos/<demo>/src del engine; F5 no puede inferir la demo. Abre y enfoca el main.cpp de la demo.",
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
# O1 (--debug): el binario arranca y SIEMPRE incluye los últimos cambios. El
# --o0 deja un A500_o0 que en este entorno no llega a arrancar (se ve 'versión
# antigua'/DOS). Para depurar variables con -O0 usa build manual y la config
# 'depurar archivo actual (optimizado)' no aplica aquí.
"$BUILD_SCRIPT" "$TARGET" --debug --clean

TARGET_NAME="$(basename "$TARGET")"
SOURCE_OUT="$ROOT/out/demos/$TARGET_NAME"
CURRENT_OUT="$ROOT/out/debug-current"
mkdir -p "$CURRENT_OUT"

# El build por configuraciones publica en out/demos/<name>/<CONFIG_ID>/ (para
# --debug sin EXTRA_DEFINES: A500_debug). Se copia SIEMPRE desde ese config;
# el fallback 'nuevo .exe' cruzado fue la causa de ejecutar un binario de otra
# config (versión antigua/incorrecta).
BUILD_START="$(date +%s)"
CONFIG_EXE=""
configs=(A500_debug A500_release A500_o0)
for c in "${configs[@]}"; do
	[ -z "$CONFIG_EXE" ] && CONFIG_EXE="$(ls -t "$SOURCE_OUT/$c"/*.exe 2>/dev/null | head -n1 || true)"
done
# Última red: el exe más reciente del target (si el config no se encuentra).
if [ -z "$CONFIG_EXE" ]; then
	CONFIG_EXE="$(ls -t "$SOURCE_OUT"/*/*.exe 2>/dev/null | head -n1 || true)"
fi
if [ -z "$CONFIG_EXE" ]; then
	echo "[debug-current] ERROR: no hay .exe para $TARGET_NAME (build por configuraciones)." >&2
	exit 1
fi
CONFIG_INFO_STAT="$(stat -c %Y "$CONFIG_EXE" 2>/dev/null || true)"
CONFIG_BASE="${CONFIG_EXE%.exe}"
echo "[debug-current] binario: $CONFIG_EXE (mtime $(date -d @"$CONFIG_INFO_STAT" 2>/dev/null || echo '?'))"
cp -f "$CONFIG_BASE.exe" "$CURRENT_OUT/current.exe"
if command -v cygpath >/dev/null 2>&1; then
	SOURCE_EXE_WIN="$(cygpath -w "$CONFIG_BASE.exe")"
	CURRENT_NO_EXT_WIN="$(cygpath -w "$CURRENT_OUT/current")"
	powershell.exe -NoProfile -Command "Copy-Item -LiteralPath '$SOURCE_EXE_WIN' -Destination '$CURRENT_NO_EXT_WIN' -Force"
else
	cp -f "$CONFIG_BASE.exe" "$CURRENT_OUT/current"
fi
cp -f "$CONFIG_BASE.elf" "$CURRENT_OUT/current.elf"
cp -f "$CONFIG_BASE.map" "$CURRENT_OUT/current.map"
cp -f "$CONFIG_BASE.s" "$CURRENT_OUT/current.s"
echo "[debug-current] current.exe actualizado -> $(stat -c %Y "$CURRENT_OUT/current.exe" 2>/dev/null || echo 'OK')"

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
