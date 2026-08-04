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
	local base
	for base in "$HOME/.cursor/extensions/bartmanabyss.amiga-debug-1.8.2/bin/win32" \
		"$HOME/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/win32"; do
		if [ -f "$base/winuae-gdb.exe" ]; then
			echo "$base/winuae-gdb.exe"
			return 0
		fi
	done
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
