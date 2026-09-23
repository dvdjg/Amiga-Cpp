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
#       [--build-all]                                      (barrido de compilacion de TODAS las demos)
#       [--protect <target>,<block|set:0xVALUE>,<size>]   (repetible; WinUAE-DBG v2.1)
#       [--fps-gate] [--fps-warn-only] [--fps-threshold <n>]  (gate de fps, opt-in)
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
BUILD_ALL=0
VISION_PROVIDER=""
VISION_SEND_MODE="multi-image"
PROTECTS=()
FPS_GATE=0
FPS_WARN_ONLY=0
FPS_THRESHOLD=""

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
		--build-all) BUILD_ALL=1; shift ;;
		--vision-provider) next_arg "$1" "$2"; VISION_PROVIDER="$2"; shift 2 ;;
		--vision-send-mode) next_arg "$1" "$2"; VISION_SEND_MODE="$2"; shift 2 ;;
		--protect) next_arg "$1" "$2"; PROTECTS+=("$2"); shift 2 ;;
		--fps-gate) FPS_GATE=1; shift ;;
		--fps-warn-only) FPS_GATE=1; FPS_WARN_ONLY=1; shift ;;
		--fps-threshold) next_arg "$1" "$2"; FPS_THRESHOLD="$2"; shift 2 ;;
		*) echo "Argumento desconocido: $1" >&2; exit 2 ;;
	esac
done

BUILD="$ROOT/tools/build/build-demo.sh"
RUN="$ROOT/tools/run/run-demo.sh"
ANALYZE="$ROOT/tools/analyze/analyze-demo.sh"
PIXEL_SELFTEST="$ROOT/tools/analyze/verify-pixel-assert.sh"
TYPE_CHECK="$ROOT/tools/check/type-tagging.mjs"
ENCODING_CHECK="$ROOT/tools/check/encoding.mjs"
LINKS_CHECK="$ROOT/tools/check/links.mjs"

TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
REPORT_DIR="$ROOT/out/regression/$TIMESTAMP"
mkdir -p "$REPORT_DIR"

# Propagar reglas protect a run-demo y analyze-sequence (WinUAE-DBG v2.1)
if [ "${#PROTECTS[@]}" -gt 0 ]; then
	export ENG_PROTECT_SPECS="${PROTECTS[*]}"
	echo "Protect specs: $ENG_PROTECT_SPECS"
fi

if [ -n "$DEMO" ]; then
	DEMO_DIRS=("$ROOT/$DEMO")
else
	DEMO_DIRS=()
	# Las demos se agrupan por plataforma: demos/<plataforma>/<demo>/ (docs/STRUCTURE.md §4).
	for p in "$ROOT"/demos/*/; do
		[ -d "$p" ] || continue
		for d in "$p"*/; do
			[ -d "$d" ] || continue
			DEMO_DIRS+=("$d")
		done
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

# --- Sistema de tipos: "el campo nace etiquetado" (INTERNAL_TYPE_SYSTEM.md §1) ---
# Falla si aparece un MemoryBlock crudo convertido a dominio (salvo excepciones
# documentadas en el propio script). Es una comprobacion estatica y barata.
if [ -f "$TYPE_CHECK" ]; then
	if command -v node >/dev/null 2>&1; then
		echo "== type-tagging =="
		if ! node "$TYPE_CHECK"; then
			echo "type-tagging fallo: hay MemoryBlock crudo convertido a dominio." >&2
			exit 1
		fi
	else
		echo "node no disponible; se omite type-tagging." >&2
	fi
fi

# --- Encoding: todo archivo de texto debe ser UTF-8 valido y sin mojibake ---
if [ -f "$ENCODING_CHECK" ]; then
	if command -v node >/dev/null 2>&1; then
		echo "== encoding =="
		if ! node "$ENCODING_CHECK"; then
			echo "encoding fallo: hay archivos no-UTF8 o con mojibake." >&2
			exit 1
		fi
	else
		echo "node no disponible; se omite encoding." >&2
	fi
fi

# --- Enlaces: la documentacion canonica no debe tener enlaces relativos rotos ---
if [ -f "$LINKS_CHECK" ]; then
	if command -v node >/dev/null 2>&1; then
		echo "== links =="
		if ! node "$LINKS_CHECK"; then
			echo "links fallo: hay enlaces relativos rotos en la documentacion." >&2
			exit 1
		fi
	else
		echo "node no disponible; se omite links." >&2
	fi
fi

# --- Regla de oro: cabeceras genericas sin escalares concretos ---
GENERIC_HEADERS_CHECK="$ROOT/tools/check/generic-headers.mjs"
if [ -f "$GENERIC_HEADERS_CHECK" ]; then
	if command -v node >/dev/null 2>&1; then
		echo "== generic-headers =="
		if ! node "$GENERIC_HEADERS_CHECK"; then
			echo "generic-headers fallo: tipo concreto en cabecera generica." >&2
			exit 1
		fi
	else
		echo "node no disponible; se omite generic-headers." >&2
	fi
fi

# --- Fachada publica: la logica de demo/juego no incluye headers cubiertos ni tipos del backend ---
API_FACADE_CHECK="$ROOT/tools/check/api-facade.mjs"
if [ -f "$API_FACADE_CHECK" ]; then
	if command -v node >/dev/null 2>&1; then
		echo "== api-facade =="
		if ! node "$API_FACADE_CHECK"; then
			echo "api-facade fallo: la logica de demo/juego debe usar eng/api/api.hpp (AGENTS 1.9)." >&2
			exit 1
		fi
	else
		echo "node no disponible; se omite api-facade." >&2
	fi
fi

# --- Docs de hallazgos: indexados en su README y con nombre kebab-case (AGENTS 1.3) ---
DOC_INDEX_CHECK="$ROOT/tools/check/doc-index.mjs"
if [ -f "$DOC_INDEX_CHECK" ]; then
	if command -v node >/dev/null 2>&1; then
		echo "== doc-index =="
		if ! node "$DOC_INDEX_CHECK"; then
			echo "doc-index fallo: doc de hallazgo huerfano o con nombre fuera de la convencion." >&2
			exit 1
		fi
	else
		echo "node no disponible; se omite doc-index." >&2
	fi
fi

# --- Gate de codegen: ningun ELF de demo con instrucciones 68020+/FPU (no corren en 68000/OCS) ---
# Audita los `.elf` ya compilados (`out/demos/**`). Falla si aparece `muls.l`/`fmove`/`extb.l`/`bf*`.
# Es la salvaguarda que sustituye a las libcalls cuando el engine las evita (ver
# docs/reference/toolchain/m68k-gcc.md). Solo corre si hay ELFs (no compila por si mismo).
ASM_AUDIT="$ROOT/tools/analyze/asm-audit.mjs"
if [ -f "$ASM_AUDIT" ] && command -v node >/dev/null 2>&1 && [ -d "$ROOT/out/demos" ]; then
	echo "== codegen (68000) =="
	if ! node "$ASM_AUDIT" --all; then
		echo "codegen fallo: hay instrucciones 68020+/FPU en un binario (no corre en 68000/OCS)." >&2
		exit 1
	fi
else
	echo "node/out-demos no disponible; se omite el gate de codegen." >&2
fi

# --- Gate de fps (opt-in): mide las demos de la bitacora y detecta deriva ---
# Lanza WinUAE por cada fila de la tabla trazable; por eso es opt-in. Falla si una
# demo medida en la misma fase (`detail`) baja del umbral (por defecto -10 %).
if [ "$FPS_GATE" -eq 1 ]; then
	FPS_CHECK="$ROOT/tools/debug/check-fps.mjs"
	if [ -f "$FPS_CHECK" ] && command -v node >/dev/null 2>&1; then
		echo "== fps gate =="
		FPS_ARGS=()
		[ -n "$FPS_THRESHOLD" ] && FPS_ARGS+=(--threshold "$FPS_THRESHOLD")
		[ "$FPS_WARN_ONLY" -eq 1 ] && FPS_ARGS+=(--warn-only)
		if ! node "$FPS_CHECK" ${FPS_ARGS[@]+"${FPS_ARGS[@]}"}; then
			echo "fps gate fallo: deriva de fps por encima del umbral." >&2
			exit 1
		fi
	else
		echo "node o check-fps no disponible; se omite fps gate." >&2
	fi
fi

# --- Codegen 68000: el camino caliente no debe llamar a libgcc (__mulsi3/__divsi3) ---
# Una operacion que deberia ser `muls.w`/`mulu.w` nativos convertida en llamada cuesta
# ~50+ ciclos. Se omite si no hay compilador cruzado disponible.
CODEGEN="$ROOT/tools/analyze/codegen-report.mjs"
if [ -f "$CODEGEN" ] && command -v node >/dev/null 2>&1; then
	CODEGEN_CXX="${AMIGA_BIN_PATH:-C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32}/opt/bin/m68k-amiga-elf-g++.exe"
	if [ -f "$CODEGEN_CXX" ]; then
		echo "== codegen (68000) =="
		if ! node "$CODEGEN"; then
			echo "codegen fallo: hay libcalls en el camino caliente." >&2
			exit 1
		fi
	else
		echo "codegen: sin compilador cruzado ($CODEGEN_CXX); se omite." >&2
	fi
fi

# --- Barrido de compilacion de TODAS las demos (opt-in: --build-all) ---------
# Distingue "asset generado ausente" (out/assets/…) de "rotura de codigo". Con
# --strict, un asset ausente tambien falla: exige el arbol de out/assets completo.
if [ "$BUILD_ALL" -eq 1 ]; then
	echo "== build-all (barrido de demos, --strict) =="
	BUILD_ALL_ARGS=(--strict)
	if [ -n "$DEMO" ]; then
		BUILD_ALL_ARGS+=(--demo "$(basename -- "$DEMO")")
	fi
	if ! bash "$ROOT/tools/build/build-all-demos.sh" "${BUILD_ALL_ARGS[@]}"; then
		echo "build-all fallo: hay demos que no compilan o tienen assets ausentes." >&2
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
			# En un barrido, el emulador de la demo anterior puede quedar colgado ocupando el
			# puerto GDB 2345 (falso fallo "puerto ocupado"); libera SOLO los PIDs que escuchan
			# esos puertos (los de este barrido). No usar el script en paralelo con otro emulador.
			run_args+=("--reset-emulator")
			# --protect se propaga por env (ENG_PROTECT_SPECS) para que llegue
			# tambien a analyze-sequence.sh sin tocar sus parsers de args.
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
			# `--vision-send-mode` solo tiene sentido con vision review; pasarlo siempre
			# rompia los `analyze-sequence.sh` por demo que no lo conocen.
			if [ "$VISION_REVIEW" -eq 1 ] || [ "$REQUIRE_VISION_REVIEW_OK" -eq 1 ]; then
				seq_args+=("--vision-send-mode" "$VISION_SEND_MODE")
			fi
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
