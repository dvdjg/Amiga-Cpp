// Test host del escalar fixed-point genérico (eng::math::Fixed<Repr,Exp>).
// Fija las INVARIANTES del sistema de tipos y del redondeo (F0 del roadmap).
#include <eng/core/fixed.hpp>

#include <cstdio>
#include <concepts>
#include <eng/retro/fixed_q.hpp>

using namespace eng::math;
using namespace eng::retro;
using eng::s16;
using eng::s32;

static int failures = 0;
static void check(bool ok, const char *msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// --- invariantes de compilación (el corazón del sistema de tipos) ---
static_assert(q12 {4096} * q12 {2048} == Fixed<s32, 24> {8388608L}, "4.12*4.12 -> 8.24 (exponentes suman)");
static_assert((q12 {4096} * q12 {2048}).rescale<12>().cast<s16>() == q12 {2048},
	      "normalizar + estrechar vuelve a 4.12");
static_assert(q12 {4096} + q12 {2048} == q12 {6144}, "suma con mismo exponente");
static_assert(from_int<s16>(3) == q0 {3}, "from_int -> exponente 0");
static_assert(q12 {8192}.rescale<0>() == q0 {2}, "4.12 -> entero (trunca");
static_assert(to_int(q12 {8192}) == 2, "to_int");

// La suma exige MISMO exponente y MISMA política. Aquí la regla se expresa con conceptos
// (consultables en compilación); que el lenguaje la imponga —y con mensaje— lo verifica
// tools/check/math-diagnostics.sh, porque un `requires { a+b; }` a secas no basta: el
// cuerpo de la sobrecarga-diagnóstico no se instancia en esa consulta.
template <typename A, typename B>
concept Sumable = requires(A a, B b) { a + b; } && A::exp == B::exp &&
                  std::same_as<typename A::policy, typename B::policy>;
template <typename A, typename B>
concept Mulable = requires(A a, B b) { a * b; };
static_assert(Sumable<q12, q12>, "4.12 + 4.12 si");
static_assert(!Sumable<q12, q0>, "4.12 + entero NO (exponentes distintos)");
static_assert(!Sumable<q12, Fixed<s16, 8>>, "4.12 + 8.8 NO (exponentes distintos)");
static_assert(Mulable<q12, q0>, "4.12 * entero si (suma de exponentes)");
// Precisiones distintas SI se combinan si el resultado es coherente: la suma promueve
// a la representacion comun y el producto alarga el exponente.
static_assert(Sumable<Fixed<s16, 12>, Fixed<s32, 12>>, "4.12(s16) + 4.12(s32) promueve");
static_assert(sizeof(decltype(Fixed<s16, 12> {} + Fixed<s32, 12> {})::repr) == 4,
	      "la suma sube a la representacion comun (s32)");
static_assert(decltype(q12 {} * Fixed<s16, 14> {})::exp == 26, "4.12 * 2.14 -> exponente 26");
// Precisión mixta de verdad: 1.0(4.12) * 1.0(2.14) = 1.0, recortado de vuelta a 4.12.
using q14 = Fixed<s16, 14>;
static_assert(sizeof(decltype(q12 {} * q14 {})::repr) == 4, "el producto mixto es de 32 bits");
static_assert((q12 {4096} * q14 {16384}).rescale<12>().cast<s16>() == q12 {4096},
	      "4.12(1.0) * 2.14(1.0) -> 4.12(1.0)");

// La representación se ENSANCHA: sin el ensanchado, 4096*2048 no cabe en s16.
static_assert(sizeof(eng::math::wide<s16>::type) == 4, "el producto es de 32 bits");

// Redondeo FUSIONADO: norm(a*b + c*d) en un paso NO es norm(a*b) + norm(c*d).
// Caso: 8191*1 + 1*1  ->  (8191+1)>>12 = 2  ;  separado  (8191>>12)+(1>>12) = 1 + 0.
static_assert(dot(q12 {8191}, q12 {1}, q12 {1}, q12 {1}) == q12 {2},
	      "dot fusionado normaliza una sola vez (mas preciso)");
static_assert((Fixed<s32, 24> {8191}.rescale<12>() + Fixed<s32, 24> {1}.rescale<12>()).cast<s16>() == q12 {1},
	      "normalizar por separado pierde el acarreo");

// Dot de tres pares (fila de matriz 3x3 por vector).
static_assert(dot(q12 {4096}, q12 {4096}, q12 {4096}, q12 {0}, q12 {4096}, q12 {0}) == q12 {4096},
	      "1*1 + 1*0 + 1*0 = 1");

// --- Políticas: MISMO layout, distinto resultado matemático -------------------
// El layout es idéntico: el mismo dato empaquetado sirve para cualquiera.
static_assert(sizeof(q12) == sizeof(q12_round) && sizeof(q12) == sizeof(q12_sat),
	      "las policies no cambian el layout");
// Redondeo: 8191/4096 = 1.9998 -> trunca 1, al más cercano 2.
static_assert(q12 {8191}.rescale<0>() == q0 {1}, "truncar hacia -inf (gratis)");
static_assert(q12_round {8191}.rescale<0>().v == s16 {2}, "redondeo al mas cercano (1 add)");
static_assert(q12_round {2048}.rescale<0>().v == s16 {1}, "0.5 -> 1 (half up)");
// Desbordamiento al estrechar: envolver vs saturar.
static_assert(Fixed<s32, 12> {200000}.cast<s16>().v == s16 {3392}, "wrap (complemento a 2)");
static_assert(Fixed<s32, 12, SaturatePolicy> {200000}.cast<s16>().v == s16 {32767}, "satura");
// `retag`: mismo layout y valor, otra política (reinterpretación sin coste).
static_assert(q12 {4096}.retag<RoundPolicy>().v == 4096, "retag no toca el valor");
// Las políticas no se mezclan sin querer.
static_assert(!Sumable<q12, q12_round>, "4.12 y 4.12-redondeado no se suman a lo bruto");

int main() {
	// Comprobaciones en runtime equivalentes (por si el optimizador oculta algo).
	const q12 a {4096}; // 1.0
	const q12 b {2048}; // 0.5
	check(a * b == Fixed<s32, 24> {8388608L}, "1.0*0.5 = 0.5 en 8.24");
	check((a * b).rescale<12>().cast<s16>() == q12 {2048}, "0.5 en 4.12");
	check(dot(q12 {8191}, q12 {1}, q12 {1}, q12 {1}) == q12 {2}, "dot fusionado");

	if (failures == 0) {
		std::printf("OK: Fixed<Repr,Exp> (exponentes, promocion, no-mezcla, dot fusionado) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
