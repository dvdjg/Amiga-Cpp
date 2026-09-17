#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Job del gate de fps (deriva de rendimiento). A diferencia de la regresion, que
# lo lleva como opt-in (--fps-gate), este wrapper esta pensado para lanzarse
# PERIODICAMENTE (Task Scheduler de Windows, cron, etc.):
#   1. asegura que cada demo de la tabla este compilada (build-demo.sh si falta),
#   2. asegura un runner.uae de esa config (run-demo.sh una vez si falta),
#   3. ejecuta tools/debug/check-fps.mjs y guarda el informe en
#      out/fps-gate/<timestamp>/report.txt.
#
# Uso: bash ./tools/run-fps-gate.sh [--samples N] [--threshold X] [--warn-only] [--no-prepare]
#   --samples N   mediciones por demo (def. 2; la mejor se compara con el baseline)
#   --threshold X fraccion minima respecto al baseline (def. 0.9 = -10 %)
#   --warn-only   no falla aunque haya deriva (util para observacion programada)
#   --no-prepare  no compila ni prepara runner.uae (asume el entorno listo)
#
# Programacion (Windows): schtasks /create /tn "AmigaCppFpsGate" /sc daily /st 03:00 \
#   /tr "bash C:\ruta\Amiga-Cpp\tools\run-fps-gate.sh --warn-only"
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BITACORA="$ROOT/docs/guides/roadmap/BITACORA_SCROLL_TILES.md"
BUILD="$ROOT/tools/build/build-demo.sh"
RUN="$ROOT/tools/run/run-demo.sh"
CHECK="$ROOT/tools/debug/check-fps.mjs"

SAMPLES=2
THRESHOLD=""
WARN_ONLY=0
PREPARE=1
while [ "$#" -gt 0 ]; do
	case "$1" in
		--samples) SAMPLES="$2"; shift 2 ;;
		--threshold) THRESHOLD="$2"; shift 2 ;;
		--warn-only) WARN_ONLY=1; shift ;;
		--no-prepare) PREPARE=0; shift ;;
		-h|--help) sed -n '2,20p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "Argumento desconocido: $1" >&2; exit 2 ;;
	esac
done

if ! command -v node >/dev/null 2>&1; then
	echo "node no disponible; no se puede ejecutar el gate de fps." >&2
	exit 1
fi
if [ ! -f "$CHECK" ]; then
	echo "falta $CHECK" >&2
	exit 1
fi

cd "$ROOT" || exit 1
TS="$(date +%Y%m%d-%H%M%S)"
OUTDIR="$ROOT/out/fps-gate/$TS"
mkdir -p "$OUTDIR"

# Filas demo|config de la tabla trazable (omite la cabecera).
ROWS="$(awk -F'|' '/^\| / { gsub(/[ `]/, "", $2); gsub(/[ `]/, "", $3); if ($2 != "" && $3 != "" && $2 != "Demo") print $2, $3 }' "$BITACORA")"

if [ "$PREPARE" -eq 1 ]; then
	while read -r demo config; do
		[ -z "${demo:-}" ] && continue
		flag="--debug"
		case "$config" in *release*) flag="--release" ;; esac
		elf="$ROOT/out/demos/$demo/$config/$demo.$config.elf"
		if [ ! -f "$elf" ]; then
			echo "== build $demo ($config) =="
			bash "$BUILD" "demos/amiga/$demo" "$flag" || echo "aviso: fallo el build de $demo" >&2
		fi
		uae="$ROOT/out/run/$demo/$config/runner.uae"
		if [ ! -f "$uae" ]; then
			echo "== preparar runner $demo ($config) =="
			bash "$RUN" "demos/amiga/$demo" >/dev/null 2>&1 || echo "aviso: fallo la preparacion de $demo" >&2
		fi
	done <<< "$ROWS"
fi

ARGS=()
[ -n "$THRESHOLD" ] && ARGS+=(--threshold "$THRESHOLD")
ARGS+=(--samples "$SAMPLES")
[ "$WARN_ONLY" -eq 1 ] && ARGS+=(--warn-only)

echo "== fps gate =="
node "$CHECK" ${ARGS[@]+"${ARGS[@]}"} 2>&1 | tee "$OUTDIR/report.txt"
status="${PIPESTATUS[0]}"
echo "[fps-gate] informe: out/fps-gate/$TS/report.txt"
exit "$status"
