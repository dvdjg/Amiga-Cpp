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

# Dominio de las matematicas de MiniFloat16: una CONSTANTE fuera de rango debe fallar
# en compilacion con un mensaje que nombre el limite (no solo saturar en runtime).
PRE3='#include <eng/core/minifloat_math.hpp>
using eng::math::MiniFloat16;'

expect_fail mf16_sin_const_oob "mf16_domain_sin_cos_must_be_within_2pi" "$PRE3
constexpr MiniFloat16 r = eng::math::sin(MiniFloat16(20.0f));"

expect_fail mf16_exp_const_oob "mf16_domain_exp_must_be_within_pm11" "$PRE3
constexpr MiniFloat16 r = eng::math::exp(MiniFloat16(50.0f));"

expect_fail mf16_log_const_oob "mf16_domain_log_must_be_positive" "$PRE3
constexpr MiniFloat16 r = eng::math::log(MiniFloat16(-1.0f));"

expect_fail mf16_sqrt_const_oob "mf16_domain_sqrt_must_be_non_negative" "$PRE3
constexpr MiniFloat16 r = eng::math::sqrt(MiniFloat16(-4.0f));"

expect_fail mf16_asin_const_oob "mf16_domain_asin_acos_must_be_within_pm1" "$PRE3
constexpr MiniFloat16 r = eng::math::asin(MiniFloat16(2.0f));"

# Control positivo: constantes en dominio, y las mismas fuera de dominio en RUNTIME
# (saturan segun contrato, sin romper la compilacion).
expect_ok mf16_domain_ok "$PRE3
constexpr MiniFloat16 a = eng::math::sin(MiniFloat16(1.0f));
constexpr MiniFloat16 b = eng::math::exp(MiniFloat16(2.0f));
constexpr MiniFloat16 c = eng::math::log(MiniFloat16(3.0f));
constexpr MiniFloat16 d = eng::math::sqrt(MiniFloat16(4.0f));
volatile float v = 50.0f;
void f() { MiniFloat16 e = eng::math::exp(MiniFloat16(v)); (void)e; }
void g() { (void)a; (void)b; (void)c; (void)d; }"

# Ruido: coordenada constante fuera del rango de celda exacta, y octavas < 1.
PRE4='#include <eng/core/noise.hpp>
using eng::math::MiniFloat16;'

expect_fail noise_coord_oob "noise_domain_coord_out_of_range" "$PRE4
constexpr MiniFloat16 r = eng::math::value_noise2(MiniFloat16(3000.0f), MiniFloat16(1.0f), 1u);"

expect_fail noise_octaves "noise_domain_octaves_must_be_positive" "$PRE4
constexpr MiniFloat16 r = eng::math::fbm2(MiniFloat16(1.0f), MiniFloat16(1.0f), 1u, 0, MiniFloat16(2.0f), MiniFloat16(0.5f));"

# Puente fix: razon constante fuera de 4.12 (±8).
PRE5='#include <eng/retro/minifloat_fixed.hpp>
using eng::math::MiniFloat16;'

expect_fail fix_ratio_oob "mf16_fix_domain_ratio_must_be_within_4_12" "$PRE5
constexpr auto r = eng::retro::mul_fixed(MiniFloat16(20.0f), eng::retro::q12 {100});"

# smootherstep necesita representar 15; un 4.12 (rango ±8) no puede.
PRE6='#include <eng/core/interp.hpp>
#include <eng/retro/fixed_q.hpp>'

expect_fail smootherstep_q12_range "no cabe en el escalar" "$PRE6
constexpr auto s = eng::math::smootherstep(eng::retro::q12 {2048});"

expect_ok domain_ok_extra "$PRE4
constexpr MiniFloat16 a = eng::math::value_noise2(MiniFloat16(1.0f), MiniFloat16(2.0f), 1u);
constexpr MiniFloat16 b = eng::math::fbm2(MiniFloat16(1.0f), MiniFloat16(2.0f), 1u, 3, MiniFloat16(2.0f), MiniFloat16(0.5f));
constexpr double sm = eng::math::smootherstep(0.5);
void g() { (void)a; (void)b; (void)sm; }"


if [ "$FAILS" -eq 0 ]; then
	echo "[math-diag] OK: diagnosticos de Fixed claros (y conversiones explicitas compilan)."
	exit 0
fi
echo "[math-diag] $FAILS comprobacion(es) fallida(s)." >&2
exit 1
