#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Copia el winuae-gdb.exe compilado (WinUAE-DBG) sobre el de la extension
# Amiga Debug de Cursor/VS Code (Windows). Sustituye a
# install-winuae-dbg-to-extension.bat con sintaxis portable.
#
# Uso: scripts/install-winuae-dbg-to-extension.sh
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$SCRIPT_DIR/../WinUAE-DBG/bin/winuae-gdb.exe"

find_extension() {
	local base entry ver best best_dir
	best_dir=""
	best=""
	for base in "$HOME/.cursor/extensions" "$HOME/.vscode/extensions"; do
		[ -d "$base" ] || continue
		for entry in "$base"/bartmanabyss.amiga-debug-*/bin/win32/winuae-gdb.exe; do
			[ -f "$entry" ] || continue
			# Version-agnostic: elige la version mas alta instalada.
			ver="$(basename "$(dirname "$(dirname "$entry")")")"
			ver="${ver#bartmanabyss.amiga-debug-}"
			if [ -z "$best" ] || [ "$ver" \> "$best" ]; then
				best="$ver"
				best_dir="$entry"
			fi
		done
	done
	if [ -n "$best_dir" ]; then
		echo "$best_dir"
		return 0
	fi
	return 1
}

if [ ! -f "$SRC" ]; then
	echo "No existe $SRC"
	echo "Compila antes: cd WinUAE-DBG && build.bat (o el build de tu plataforma)"
	exit 1
fi

EXT="$(find_extension)" || { echo "No se encontro la extension amiga-debug en Cursor/VS Code." >&2; exit 1; }
if [ ! -f "$EXT.bak" ]; then
	cp "$EXT" "$EXT.bak"
fi
cp "$SRC" "$EXT"
echo "OK: $SRC -> $EXT"
echo "Reinicia la sesion de depuracion en Cursor."
