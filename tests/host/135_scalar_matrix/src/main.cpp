// ============================================================================
// Test HOST-135: matriz de escalares (referencia y comparacion entre anchos)
// ============================================================================
//
// Instancia los MISMOS algoritmos genericos con varios escalares y compara contra una
// referencia `double`, midiendo el error relativo maximo: sirve de referencia de como se
// comporta cada ancho (16 bits retro vs 32 bits vs nativo). Ejercita ademas la division y
// la raiz de `Fixed<s32,E>` (F1 del roadmap del escalar generico).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/135_scalar_matrix
//   (con -DENG_SCALAR_RETRO16 simula la seleccion de 16 bits en host)

#include <cstdio>
#include <cmath>

#include <eng/core/fixed.hpp>
#include <eng/core/fixed_math.hpp>
#include <eng/core/geometry.hpp>
#include <eng/core/interp.hpp>
#include <eng/core/minifloat.hpp>
#include <eng/core/minifloat_math.hpp>
#include <eng/core/noise.hpp>
#include <eng/core/numeric_traits.hpp>
#include <eng/core/scalar.hpp>
#include <eng/core/scalar_math.hpp>
#include <eng/core/scalar_ops.hpp>

namespace {

using namespace eng::math;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

[[nodiscard]] double rel(double got, double ref) {
	const double d = std::fabs(got - ref);
	return d / (std::fabs(ref) > 1e-12 ? std::fabs(ref) : 1.0);
}

/// Error maximo de `lerp`, `smoothstep` y `normalize` de `S` respecto a `double`.
template <class S>
void run_scalar(const char* name, double tol) {
	const S zero = scalar_traits<S>::from_int(0);
	const S one = scalar_traits<S>::one();
	double e_lerp = 0.0;
	double e_smooth = 0.0;
	for (int i = 0; i <= 10; ++i) {
		const double t = static_cast<double>(i) / 10.0;
		const S ts = scalar_const<S>::from(t);
		e_lerp = std::fmax(e_lerp, rel(to_double(lerp(zero, one, ts)), t));
		e_smooth = std::fmax(e_smooth,
				     rel(to_double(smoothstep(ts)), t * t * (3.0 - 2.0 * t)));
	}
	const Vec<2, S> v {{one, one}};
	const Vec<2, S> n = normalize(v);
	const double e_norm = rel(to_double(n.v[0]), 0.7071067811865476);

	std::printf("  %-14s lerp=%.2e smooth=%.2e norm=%.2e (tol %.0e)\n", name, e_lerp,
		    e_smooth, e_norm, tol);
	check(e_lerp < tol, "lerp dentro de tolerancia");
	check(e_smooth < tol, "smoothstep dentro de tolerancia");
	check(e_norm < tol, "normalize dentro de tolerancia");
}

void test_scalars() {
	run_scalar<double>("double", 1e-12);
	run_scalar<float>("float", 1e-5);
	run_scalar<MiniFloat16>("MiniFloat16", 5e-3);
	run_scalar<Fixed<eng::s16, 12>>("Fixed<s16,12>", 1e-2);
	run_scalar<Fixed<eng::s32, 12>>("Fixed<s32,12>", 1e-2);
	run_scalar<Fixed<eng::s32, 24>>("Fixed<s32,24>", 1e-5);
}

/// Division y raiz de `Fixed<s32,E>` (F1): en host deben funcionar.
void test_fixed32_ops() {
	using F32 = Fixed<eng::s32, 12>;
	const F32 half = div_norm(F32 {4096}, F32 {8192}); // 1.0 / 2.0
	check(half.v == 2048, "Fixed<s32,12>: div_norm 1/2 = 0.5");

	const F32 rt = scalar_sqrt<F32>::op(F32 {16384}); // sqrt(4.0)
	check(rt.v == 8192, "Fixed<s32,12>: sqrt(4.0) = 2.0");

	// Valores no exactos: comprobar contra double.
	const F32 third = div_norm(F32 {4096}, F32 {12288}); // 1.0 / 3.0
	check(rel(to_double(third), 1.0 / 3.0) < 1e-2, "Fixed<s32,12>: 1/3 con error < 1e-2");
	const F32 r3 = scalar_sqrt<F32>::op(F32 {12288}); // sqrt(3.0)
	check(rel(to_double(r3), std::sqrt(3.0)) < 1e-2, "Fixed<s32,12>: sqrt(3) con error < 1e-2");
}

/// F2: la trigonometria/exponencial de `Fixed<s32,E>` (host) funciona.
void test_fixed32_math() {
	using F = Fixed<eng::s32, 12>;
	const F one = scalar_traits<F>::one();
	const F six = scalar_const<F>::from(0.5235987755982988); // pi/6
	check(rel(to_double(scalar_sin<F>::op(six)), 0.5) < 5e-3, "Fixed<s32,12>: sin(pi/6)");
	check(rel(to_double(scalar_cos<F>::op(six)), 0.8660254037844386) < 5e-3,
	      "Fixed<s32,12>: cos(pi/6)");
	check(rel(to_double(scalar_atan2<F>::op(one, one)), 0.7853981633974483) < 5e-3,
	      "Fixed<s32,12>: atan2(1,1)");
	check(rel(to_double(scalar_exp2<F>::op(one)), 2.0) < 5e-3, "Fixed<s32,12>: exp2(1)");
	check(rel(to_double(scalar_log2<F>::op(scalar_traits<F>::from_int(4))), 2.0) < 5e-3,
	      "Fixed<s32,12>: log2(4)");
}

/// F2: operaciones que en 4.12 (`s16`) fallan por **rango** pero caben con 32 bits:
/// `smootherstep` (coeficiente 15 > ±8) y el ruido (`value_noise`/`fbm`, rejilla de 1024
/// niveles normalizada con `div_norm`).
void test_fixed32_extra() {
	using F = Fixed<eng::s32, 12>;
	const F half = scalar_const<F>::from(0.5);
	check(rel(to_double(smootherstep(half)), 0.5) < 5e-3,
	      "Fixed<s32,12>: smootherstep(0.5) = 0.5");

	const double n = to_double(value_noise1(scalar_const<F>::from(3.7), 42u));
	check(n >= 0.0 && n <= 1.0, "Fixed<s32,12>: value_noise1 en [0,1]");

	const F f = fbm1(scalar_const<F>::from(1.0), 7u, 4, scalar_const<F>::from(2.0),
			 scalar_const<F>::from(0.5));
	const double fd = to_double(f);
	check(fd >= 0.0 && fd <= 1.0, "Fixed<s32,12>: fbm1 (4 octavas) en [0,1]");

	// `ease_*_expo` usa las constantes 10 y 20: no caben en 4.12 (±8) pero si en 32 bits.
	check(rel(to_double(ease_in_expo(half)), 0.03125) < 5e-3,
	      "Fixed<s32,12>: ease_in_expo(0.5) = 2^-5");
}

/// Escalar entero general (`eng::intw`) y nativos con las operaciones exactas.
void test_integers() {
	using I = eng::intw;
	check(min<I>(I {3}, I {5}) == I {3}, "intw: min");
	check(max<I>(I {3}, I {5}) == I {5}, "intw: max");
	check(abs(I {-4}) == I {4}, "intw: abs");
	check(clamp(I {7}, I {0}, I {5}) == I {5}, "intw: clamp");

	std::printf("  scalar_mode=%s  sizeof(intw)=%u\n", eng::scalar_mode,
		    static_cast<unsigned>(sizeof(eng::intw)));
}

} // namespace

int main() {
	std::printf("ScalarMatrix:\n");
	test_scalars();
	test_fixed32_ops();
	test_fixed32_math();
	test_fixed32_extra();
	test_integers();

	if (g_fail == 0u) {
		std::printf("OK: matriz de escalares (double/float/MF/Fixed s16 y s32) validada\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
