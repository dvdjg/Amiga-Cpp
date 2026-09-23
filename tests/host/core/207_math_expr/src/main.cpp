// ============================================================================
// Test HOST-207: expression templates lite (`eng/core/expr.hpp`)
// ============================================================================
//
// Valida que el arbol de expresion se evalua una sola vez y de forma correcta en tres
// frentes: escalares nativos (`double`), escalares con formato propio (`MiniFloat16`) y
// fixed-point (`Fixed`, cuyo producto cambia de exponente), ademas de la evaluacion
// FUSIONADA por componente para `Vec`/`Mat` (un solo bucle, sin contenedores temporales).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/207_math_expr

#include <cstdio>

#include <eng/core/expr.hpp>
#include <eng/core/fixed.hpp>
#include <eng/core/linalg.hpp>
#include <eng/core/minifloat.hpp>
#include <eng/core/numeric_traits.hpp>

namespace {

using namespace eng::math;
using namespace eng::math::et;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Evaluar en tiempo de compilacion demuestra que el arbol es `constexpr` de verdad.
constexpr double ka = 2.0;
constexpr double kb = 3.0;
constexpr double kc = 4.0;
constexpr double kd = 5.0;
static_assert(evaluate<double>(val(ka) + val(kb) * val(kc) - val(kd)) == 2.0 + 3.0 * 4.0 - 5.0,
	      "expr: a + b*c - d debe plegarse en compilacion");
static_assert(evaluate<double>(-(val(ka) * val(kb))) == -6.0, "expr: negacion unaria");

void test_native_scalar() {
	// El arbol respeta la precedencia: a + b*c - d.
	const double got = evaluate<double>(val(ka) + val(kb) * val(kc) - val(kd));
	check(got == 2.0 + 3.0 * 4.0 - 5.0, "expr: double respeta precedencia");

	// Mezcla expression + valor crudo por los dos lados.
	check(evaluate<double>(val(ka) + kb * kc) == ka + kb * kc, "expr: expr + crudo");
	check(evaluate<double>(ka + val(kb) * kc) == ka + kb * kc, "expr: crudo + expr");
	check(evaluate<double>(val(ka) * val(kb) / val(kc)) == ka * kb / kc, "expr: * y /");
}

void test_minifloat() {
	const MiniFloat16 a {0.5f};
	const MiniFloat16 b {0.25f};
	const MiniFloat16 c {2.0f};
	const MiniFloat16 ref = a + b * c - b;
	const MiniFloat16 got = evaluate<MiniFloat16>(val(a) + val(b) * val(c) - val(b));
	check(got.raw == ref.raw, "expr: MiniFloat16 identico a la evaluacion directa");
}

void test_fixed_exponent() {
	// 4.12: 0.5, 0.25 y 1.0.
	const Fixed<eng::s16, 12> a {2048};
	const Fixed<eng::s16, 12> b {1024};
	const Fixed<eng::s16, 12> c {4096};

	// Suma (mismo exponente): resultado directo.
	check(evaluate<Fixed<eng::s16, 12>>(val(a) + val(b)).v == 3072, "expr: Fixed suma en 4.12");

	// Producto: el arbol multiplica en 4.24 (s32) y `eval<4.12>` reescala y moldea.
	check(evaluate<Fixed<eng::s16, 12>>(val(a) * val(b)).v == 512, "expr: Fixed producto reescala");

	// Acumulador ancho: `a + b*c` mezcla un termino en 4.12 con un producto en 4.24; `et_add`
	// promueve `a` a 4.24 (desplazamiento exacto), suma y `eval<4.12>` normaliza UNA vez.
	check(evaluate<Fixed<eng::s16, 12>>(val(a) + val(b) * val(c)).v == 3072,
	      "expr: Fixed a + b*c con acumulador ancho");

	// `dot` como expresion: suma de productos del mismo exponente, normalizada una vez.
	check(evaluate<Fixed<eng::s16, 12>>(val(a) * val(a) + val(b) * val(b)).v == 1280,
	      "expr: Fixed suma de productos");

	// Cadena del mismo exponente (suma/resta).
	check(evaluate<Fixed<eng::s16, 12>>(val(a) + val(b) - val(c)).v == -1024,
	      "expr: Fixed cadena del mismo exponente");

	// Al tipo destino de 8.8 (otro exponente) debe reescalar, no reinterpretar.
	check(evaluate<Fixed<eng::s16, 8>>(val(a) + val(b)).v == 192, "expr: Fixed destino distinto exponente");
}

void test_vector_fusion() {
	const Vec<3, double> p {{1.0, 2.0, 3.0}};
	const Vec<3, double> q {{4.0, 5.0, 6.0}};
	const Vec<3, double> r {{7.0, 8.0, 9.0}};

	// a + b*2 - c, fusionado en un solo bucle.
	Vec<3, double> out {};
	eval_into(out, val(p) + val(q) * 2.0 - val(r));
	check(out.v[0] == 2.0 && out.v[1] == 4.0 && out.v[2] == 6.0, "expr: Vec fusionado");

	// Difundir un escalar llena todo el vector.
	Vec<3, double> fill {};
	eval_into(fill, val(3.5));
	check(fill.v[0] == 3.5 && fill.v[1] == 3.5 && fill.v[2] == 3.5, "expr: broadcast de escalar");

	// `assign_to` equivale a `eval_into`.
	Vec<3, double> via {};
	(val(p) + val(q)).assign_to(via);
	check(via.v[0] == 5.0 && via.v[1] == 7.0 && via.v[2] == 9.0, "expr: assign_to");
}

void test_matrix_fusion() {
	const Mat<2, double> A {{{1.0, 2.0}, {3.0, 4.0}}};
	const Mat<2, double> B {{{5.0, 6.0}, {7.0, 8.0}}};

	Mat<2, double> D {};
	eval_into(D, val(A) * 0.5 + val(B));
	check(D.m[0][0] == 5.5 && D.m[0][1] == 7.0 && D.m[1][0] == 8.5 && D.m[1][1] == 10.0,
	      "expr: Mat fusionado (elemento a elemento)");
}

void test_fixed_vector() {
	// Componentes fixed: el producto de cada componente sube a 4.24 y `et_set` lo reescala
	// al escalar del vector destino, todo dentro del mismo bucle.
	using F = Fixed<eng::s16, 12>;
	const Vec<2, F> a {{F {2048}, F {1024}}}; // {0.5, 0.25}
	const Vec<2, F> b {{F {2048}, F {4096}}}; // {0.5, 1.0}
	const Vec<2, F> c {{F {8192}, F {8192}}}; // {2.0, 2.0}

	Vec<2, F> out {};
	eval_into(out, val(a) * val(b));
	check(out.v[0].v == 1024 && out.v[1].v == 1024, "expr: Vec<Fixed> fusionado (producto reescalado)");

	// Termino + producto por componente: `et_add` promueve la componente a 4.24, acumula y
	// `eval_into` normaliza al escribir en el vector 4.12. {0.5+1.0, 0.25+2.0}.
	Vec<2, F> mix {};
	eval_into(mix, val(a) + val(b) * val(c));
	check(mix.v[0].v == 6144 && mix.v[1].v == 9216, "expr: Vec<Fixed> con acumulador ancho");
}

} // namespace

int main() {
	std::printf("eng::math::et expression templates:\n");
	test_native_scalar();
	test_minifloat();
	test_fixed_exponent();
	test_vector_fusion();
	test_matrix_fusion();
	test_fixed_vector();

	if (g_fail == 0u) {
		std::printf("OK: expression templates lite (escalar constexpr, Fixed, MF16, Vec/Mat "
			    "fusionados)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
