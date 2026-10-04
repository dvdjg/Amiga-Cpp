#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# host-cxx-env.sh: entorno comun de los scripts que COMPILAN y EJECUTAN un
# binario host con g++.
#
# En Windows/MinGW dos detalles rompen la ejecucion:
#   1. Las DLLs de runtime (libstdc++/libgcc) deben ser las del compilador que
#      enlazo el binario; si en el PATH aparece la de otra instalacion de MinGW,
#      el cargador falla (Git Bash lo reporta como 127 / "Exec format error").
#   2. g++ anade `.exe`, de modo que ejecutar la ruta sin extension puede fallar
#      o recoger un binario viejo homonimo.
#
# Se usa con `source`:
#   . "$ROOT/tools/scripts/host-cxx-env.sh"
#   host_cxx_prepare "$CXX"
#   "$CXX" ... -o "$BIN"
#   "$(host_exe "$BIN")" "$@"
# ---------------------------------------------------------------------------

# Antepone el `bin` del compilador al PATH (no-op si no se encuentra).
host_cxx_prepare() {
	local cxx="${1:-g++}" path
	path="$(command -v "$cxx" 2>/dev/null)" || return 0
	export PATH="$(dirname "$path"):$PATH"
}

# Imprime la ruta ejecutable real: `<bin>.exe` si existe (MinGW), si no `<bin>`.
host_exe() {
	if [ -f "$1.exe" ]; then
		printf '%s\n' "$1.exe"
	else
		printf '%s\n' "$1"
	fi
}
