#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Comprueba que los diagnosticos de compilacion de eng::math::Fixed son CLAROS:
# mezclar exponentes (suma/resta/comparacion) DEBE fallar con el mensaje que
# explica la conversion explicita, no con un "no matching function" opaco.
# La calidad del error es parte del API (docs/engine/architecture/MATH_LIBRARY.md).
#
# Uso: tools/check/math-diagnostics.sh
# Necesita el g++ del host (CXX) y el engine en engine/include.
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
FLAGS=(-std=gnu++23 "-I$ROOT/engine/include" -fsyntax-only)
TMP="$ROOT/out/tmp/math-diagnostics"
mkdir -p "$TMP"

FAILS=0

expect_fail() {
	local name="$1" needle="$2" src="$3" err
	local f="$TMP/$name.cpp"
	printf '%s\n' "$src" >"$f"
	if err=$("$CXX" "${FLAGS[@]}" "$f" 2>&1); then
		echo "[math-diag] FAIL: '$name' compilo cuando debia fallar" >&2
		FAILS=$((FAILS + 1))
		return
	fi
	if ! printf '%s' "$err" | grep -q "$needle"; then
		echo "[math-diag] FAIL: '$name' fallo sin el mensaje esperado ('$needle')" >&2
		printf '%s\n' "$err" | tail -6 >&2
		FAILS=$((FAILS + 1))
		return
	fi
	echo "[math-diag] ok: $name"
}

expect_ok() {
	local name="$1" src="$2"
	local f="$TMP/$name.cpp"
	printf '%s\n' "$src" >"$f"
	if ! "$CXX" "${FLAGS[@]}" "$f"; then
		echo "[math-diag] FAIL: '$name' no compilo cuando debia" >&2
		FAILS=$((FAILS + 1))
		return
	fi
	echo "[math-diag] ok: $name"
}

PRE='#include <eng/core/fixed.hpp>
#include <eng/retro/fixed_q.hpp>
using eng::retro::q12;
using eng::retro::q0;'

expect_fail add_mismatch "distinto exponente" "$PRE
void f() { q12 a {4096}; q0 b {3}; auto r = a + b; (void)r; }"

expect_fail sub_mismatch "distinto exponente" "$PRE
void f() { q12 a {4096}; q0 b {3}; auto r = a - b; (void)r; }"

expect_fail cmp_mismatch "distinto exponente" "$PRE
void f() { q12 a {4096}; q0 b {3}; bool x = (a == b); (void)x; }"

# Control positivo: la MISMA exponente si, el producto combina exponentes, y las
# conversiones explicitas (norm/from_int) habilitan la mezcla.
expect_ok same_exp_ok "$PRE
void f() {
	q12 a {4096}, c {2048};
	auto s = a + c;      // 4.12 + 4.12
	auto p = a * c;      // 4.12 * 4.12 -> 8.24
	auto d = a.rescale<8>();              // 4.12 -> 4.8 explicito
	auto m = eng::math::from_int<eng::s16>(3) + eng::retro::q0 {2};
	(void)s; (void)p; (void)d; (void)m;
}"

# Mat/Vec: dimensionalidad y escalares.
PRE2='#include <eng/core/linalg.hpp>
#include <eng/retro/fixed_q.hpp>
using namespace eng::math;
using namespace eng::retro;'

expect_fail mat_vec_scalar_mismatch "escalares incompatibles" "$PRE2
void f() { Mat<3, float> m {}; Vec<3, q12> v {}; auto r = m * v; (void)r; }"

expect_fail vec_dim_mismatch "distinta dimension" "$PRE2
void f() { Vec<3, q12> a {}; Vec<2, q12> b {}; auto r = a + b; (void)r; }"

expect_fail vec_scalar_mismatch "escalares distintos" "$PRE2
void f() { Vec<3, q12> a {}; Vec<3, float> b {}; auto r = a - b; (void)r; }"

expect_ok mat_vec_ok "$PRE2
void f() {
	Mat<3, q12> m = Mat<3, q12>::identity();
	const Vec<3, q0> v {};
	auto r = m * v; // ratio 4.12 * longitud 0 -> longitud 0 (si)
	(void)r;
}"


if [ "$FAILS" -eq 0 ]; then
	echo "[math-diag] OK: diagnosticos de Fixed claros (y conversiones explicitas compilan)."
	exit 0
fi
echo "[math-diag] $FAILS comprobacion(es) fallida(s)." >&2
exit 1
