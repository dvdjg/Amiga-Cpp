#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Menu interactivo para compilar, lanzar, depurar y analizar demos/tests.
# Sustituye a demo-menu.ps1. Uso: tools/run/demo-menu.sh
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$ROOT/tools/lib/demos.sh"
BUILD="$ROOT/tools/build/build-demo.sh"
RUN="$ROOT/tools/run/run-demo.sh"
ANALYZE="$ROOT/tools/analyze/analyze-demo.sh"
CHANNEL="$ROOT/tools/debug/winuae-side-channel.sh"

echo "Demos disponibles:"
# Descubrimiento por contenido (dirs con src/*.cpp): la profundidad varia con techniques/features.
mapfile -t DEMOS < <(list_demos_rel "$ROOT")
i=0
for d in "${DEMOS[@]}"; do
	echo "  $i) $d"
	i=$((i + 1))
done
echo -n "Elige una demo (0-$((i - 1))): "
read -r choice
if ! [[ "$choice" =~ ^[0-9]+$ ]] || [ "$choice" -ge "$i" ]; then
	echo "Seleccion invalida." >&2
	exit 1
fi
demo="${DEMOS[$choice]}"

echo "Acciones:"
echo "  1) Compilar (debug)    2) Compilar (release)    3) Ejecutar y capturar"
echo "  4) Analizar            5) Canal lateral         6) Regresion"
echo -n "Elige una accion: "
read -r action

case "$action" in
	1) "$BUILD" "$demo" --debug --clean ;;
	2) "$BUILD" "$demo" --clean ;;
	3) "$RUN" "$demo" ;;
	4) "$ANALYZE" "$demo" ;;
	5) echo "Escribe una orden del canal lateral (state, regs, mem <addr> <len>, ...):"
	   read -r -a cmd
	   "$CHANNEL" "${cmd[@]}" ;;
	6) "$ROOT/tools/test-regression.sh" --demo "$demo" ;;
	*) echo "Accion invalida." >&2; exit 1 ;;
esac
