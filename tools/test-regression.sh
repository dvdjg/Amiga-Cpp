#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Regresion completa del engine: para cada demo ejecuta build -> run -> analyze
# y, si la demo tiene analyze-sequence.sh, la secuencia de verificacion temporal
# (FrameScope, PixelAssert, Vision Review). Genera un informe Markdown/JSON en
# out/regression/<timestamp>/. Sustituye a test-regression.ps1.
#
# Uso: tools/test-regression.sh [--demo <ruta>] [--release-build] [--skip-run]
#       [--keep-going] [--warp] [--pixel-assert] [--require-pixel-assert-ok]
#       [--pixel-assert-selftest] [--vision-review] [--require-vision-review-ok]
#       [--vision-provider <ruta>] [--vision-send-mode multi-image|contact-sheet]
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# --- Argumentos -------------------------------------------------------------
DEMO=""
RELEASE_BUILD=0
SKIP_RUN=0
KEEP_GOING=0
WARP=0
VISION_REVIEW=0
REQUIRE_VISION_REVIEW_OK=0
PIXEL_ASSERT=0
REQUIRE_PIXEL_ASSERT_OK=0
PIXEL_ASSERT_SELFTEST=0
VISION_PROVIDER=""
VISION_SEND_MODE="multi-image"

next_arg() {
	if [ -z "${2:-}" ]; then
		echo "Falta el valor de $1" >&2
		exit 2
	fi
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--demo) next_arg "$1" "$2"; DEMO="$2"; shift 2 ;;
		--release-build) RELEASE_BUILD=1; shift ;;
		--skip-run) SKIP_RUN=1; shift ;;
		--keep-going) KEEP_GOING=1; shift ;;
		--warp) WARP=1; shift ;;
		--vision-review) VISION_REVIEW=1; shift ;;
		--require-vision-review-ok) REQUIRE_VISION_REVIEW_OK=1; shift ;;
		--pixel-assert) PIXEL_ASSERT=1; shift ;;
		--require-pixel-assert-ok) REQUIRE_PIXEL_ASSERT_OK=1; shift ;;
		--pixel-assert-selftest) PIXEL_ASSERT_SELFTEST=1; shift ;;
		--vision-provider) next_arg "$1" "$2"; VISION_PROVIDER="$2"; shift 2 ;;
		--vision-send-mode) next_arg "$1" "$2"; VISION_SEND_MODE="$2"; shift 2 ;;
		*) echo "Argumento desconocido: $1" >&2; exit 2 ;;
	esac
done

BUILD="$ROOT/tools/build/build-demo.sh"
RUN="$ROOT/tools/run/run-demo.sh"
ANALYZE="$ROOT/tools/analyze/analyze-demo.sh"
PIXEL_SELFTEST="$ROOT/tools/analyze/verify-pixel-assert.sh"

TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
REPORT_DIR="$ROOT/out/regression/$TIMESTAMP"
mkdir -p "$REPORT_DIR"

if [ -n "$DEMO" ]; then
	DEMO_DIRS=("$ROOT/$DEMO")
else
	DEMO_DIRS=()
	for d in "$ROOT"/demos/*/; do
		DEMO_DIRS+=("$d")
	done
fi

if [ ${#DEMO_DIRS[@]} -eq 0 ]; then
	echo "No se encontraron demos para ejecutar." >&2
	exit 1
fi

if [ "$PIXEL_ASSERT_SELFTEST" -eq 1 ]; then
	echo "== pixel-assert selftest =="
	if ! "$PIXEL_SELFTEST"; then
		echo "Pixel Assert selftest fallo." >&2
		exit 1
	fi
fi

MD="$REPORT_DIR/regression-report.md"
{
	echo "# Regression $TIMESTAMP"
	echo ""
	echo "| Demo | Build | Run | Analyze | Sequence | PixelAssert | Notes |"
	echo "|---|---:|---:|---:|---:|---:|---|"
} >"$MD"

JSON_RESULTS="[]"
FAILED=0

for demo_path in "${DEMO_DIRS[@]}"; do
	demo_name="$(basename "$demo_path")"
	# build/run/analyze esperan rutas relativas a ROOT (demos/<nombre>/).
	relative_demo="${demo_path#"$ROOT"/}"
	build="pending"; run="skipped"; analyze="pending"; sequence="none"; pixel_assert="none"; notes=""

	echo "== ${demo_name}: build =="
	build_args=("$BUILD" "$relative_demo")
	if [ "$RELEASE_BUILD" -eq 0 ]; then
		build_args+=("--debug")
	fi
	if ! "${build_args[@]}"; then
		build="fail"; notes="build"
	else
		build="ok"
		if [ "$SKIP_RUN" -eq 0 ]; then
			echo "== ${demo_name}: run =="
			run="pending"
			run_args=("$RUN" "$relative_demo")
			if [ "$WARP" -eq 1 ]; then run_args+=("--warp"); fi
			if "${run_args[@]}"; then
				run="ok"
				echo "== ${demo_name}: analyze =="
				analyze="pending"
				if "$ANALYZE" "$relative_demo"; then
					analyze="ok"
				else
					analyze="fail"; notes="analyze"
				fi
			else
				run="fail"; notes="run"
			fi
		else
			analyze="pending"
			echo "== ${demo_name}: analyze =="
			if "$ANALYZE" "$relative_demo"; then analyze="ok"; else analyze="fail"; notes="analyze"; fi
		fi

		sequence_script="$demo_path/analyze-sequence.sh"
		if [ "$SKIP_RUN" -eq 0 ] && [ -f "$sequence_script" ]; then
			echo "== ${demo_name}: sequence =="
			sequence="pending"
			[ "$PIXEL_ASSERT" -eq 1 ] || [ "$REQUIRE_PIXEL_ASSERT_OK" -eq 1 ] && pixel_assert="pending"
			seq_args=("$sequence_script")
			[ "$WARP" -eq 1 ] && seq_args+=("--warp")
			[ "$PIXEL_ASSERT" -eq 1 ] && seq_args+=("--pixel-assert")
			[ "$REQUIRE_PIXEL_ASSERT_OK" -eq 1 ] && seq_args+=("--require-pixel-assert-ok")
			[ "$VISION_REVIEW" -eq 1 ] && seq_args+=("--vision-review")
			[ "$REQUIRE_VISION_REVIEW_OK" -eq 1 ] && seq_args+=("--require-vision-review-ok")
			[ -n "$VISION_PROVIDER" ] && seq_args+=("--vision-provider" "$VISION_PROVIDER")
			seq_args+=("--vision-send-mode" "$VISION_SEND_MODE")
			if "${seq_args[@]}"; then
				sequence="ok"
				{ [ "$PIXEL_ASSERT" -eq 1 ] || [ "$REQUIRE_PIXEL_ASSERT_OK" -eq 1 ]; } && pixel_assert="ok"
			else
				sequence="fail"; notes="sequence"
			fi
		fi
	fi

	{
		echo "| $demo_name | $build | $run | $analyze | $sequence | $pixel_assert | $notes |"
	} >>"$MD"

	if [ "$build" != "ok" ] || { [ "$run" != "ok" ] && [ "$run" != "skipped" ]; } || \
	   [ "$analyze" != "ok" ] || { [ "$sequence" != "ok" ] && [ "$sequence" != "none" ]; } || \
	   { [ "$pixel_assert" != "ok" ] && [ "$pixel_assert" != "none" ]; }; then
		FAILED=$((FAILED + 1))
		if [ "$KEEP_GOING" -eq 0 ]; then
			break
		fi
	fi
done

echo ""
echo "Regression report: $MD"
if [ "$FAILED" -gt 0 ]; then
	echo "Regression failed: $FAILED demo(s)." >&2
	exit 1
fi
echo "Regression OK."
exit 0
