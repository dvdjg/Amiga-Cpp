#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Compila TODAS las demos de una plataforma y CLASIFICA los fallos.
#
# Uso: tools/build/build-all-demos.sh [--platform <p>] [--demo <subcadena>]
#                                     [--release] [--no-clean] [--strict]
#
# Por defecto compila limpio (--debug --clean) para no ocultar roturas con
# objetos obsoletos. Cada demo se clasifica en tres estados:
#
#   OK    -> compila y enlaza.
#   ASSET -> falla porque falta un asset GENERADO en out/ (un `.raw` de audio,
#            un header del pipeline de tiles, …). No es un fallo de codigo.
#   FAIL  -> error de compilacion/enlace: rotura de codigo.
#   SKIP  -> demo marcada "a adaptar" en tools/build/skip-demos.txt (no cuenta
#            como fallo; se rehace con el mini-OS + la API certificada).
#
# Codigo de salida: 1 si hay algun FAIL; 0 si solo hay ASSET. Con --strict
# tambien devuelve 1 si hay ASSET (para exigir el arbol de assets completo).
#
# Detalle de cada fallo en out/tmp/build-all.log.
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT/tools/lib/demos.sh"

PLATFORM=""
PATTERN=""
BUILD_FLAG="--debug"
CLEAN="--clean"
STRICT=0

while [ $# -gt 0 ]; do
	case "$1" in
		--platform) PLATFORM="${2:?falta el valor de --platform}"; shift 2 ;;
		--demo) PATTERN="${2:?falta el valor de --demo}"; shift 2 ;;
		--debug) BUILD_FLAG="--debug"; shift ;;
		--release) BUILD_FLAG="--release"; shift ;;
		--no-clean) CLEAN=""; shift ;;
		--strict) STRICT=1; shift ;;
		-h | --help) sed -n '2,20p' "$0"; exit 0 ;;
		*) echo "Argumento desconocido: $1" >&2; exit 2 ;;
	esac
done

# Un asset GENERADO ausente lo delatan el ensamblador (`.incbin`) o el include
# del preprocesador; ambos nombran una ruta bajo `out/assets/`.
ASSET_RE='file not found: out/assets/|out/assets/[A-Za-z0-9_./-]+: No such file or directory'

mkdir -p "$ROOT/out/tmp"
LOG="$ROOT/out/tmp/build-all.log"
ONE="$ROOT/out/tmp/build-all-one.log"
: >"$LOG"

ok=0
asset=0
fail=0
skip=0
SKIP_LIST="$ROOT/tools/build/skip-demos.txt"

for rel in $(list_demos_rel "$ROOT"); do
	name="$(basename "$rel")"
	if [ -n "$PATTERN" ]; then
		case "$name" in *"$PATTERN"*) ;; *) continue ;; esac
	fi
	if [ -n "$PLATFORM" ]; then
		case "$rel" in *"$PLATFORM"*) ;; *) continue ;; esac
	fi
	if [ -f "$SKIP_LIST" ] && grep -qxF "$rel" "$SKIP_LIST"; then
		skip=$((skip + 1))
		printf 'SKIP  %s\n' "$name"
		continue
	fi

	if bash "$ROOT/tools/build/build-demo.sh" "$rel" "$BUILD_FLAG" $CLEAN >"$ONE" 2>&1; then
		ok=$((ok + 1))
		printf 'OK    %s\n' "$name"
	elif grep -qE "$ASSET_RE" "$ONE"; then
		asset=$((asset + 1))
		printf 'ASSET %s\n' "$name"
		{
			echo "===== $name (asset generado ausente) ====="
			grep -oE 'out/assets/[A-Za-z0-9_./-]+' "$ONE" | sort -u | sed 's/^/  falta: /'
		} >>"$LOG"
	else
		fail=$((fail + 1))
		printf 'FAIL  %s\n' "$name"
		{
			echo "===== $name (fallo de codigo) ====="
			grep -E 'error:|Error [0-9]|undefined reference' "$ONE" | head -6
		} >>"$LOG"
	fi
done

echo ""
echo "TOTAL ok=$ok asset=$asset fail=$fail skip=$skip  (detalle: out/tmp/build-all.log)"

if [ "$fail" -gt 0 ]; then
	exit 1
fi
if [ "$STRICT" -eq 1 ] && [ "$asset" -gt 0 ]; then
	exit 1
fi
exit 0
