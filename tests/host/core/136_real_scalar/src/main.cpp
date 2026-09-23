// ============================================================================
// Test HOST-136: tipos generales de simulacion (eng::real / eng::coord / eng::intw)
// ============================================================================
//
// El MISMO codigo, escrito contra los alias de `eng/core/math/scalar.hpp`, se compila en los
// tres modos del escalar: RETRO16 (s16 / Fixed<s16,12>), RETRO32 (s32 / Fixed<s32,12>) y
// NATIVE (int / float). El script `tools/run/run-scalar-modes.sh` compila este fuente con
// `-DENG_SCALAR_RETRO16`, `-DENG_SCALAR_RETRO32` y sin macro, y compara las salidas. En el
// runner normal (host sin macro) corre en modo nativo.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/core/136_real_scalar
//   bash tools/run/run-scalar-modes.sh           (compara los tres modos)

#include <cmath>
#include <cstdio>
#include <cstring>

#include <eng/core/math/interp.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/numeric_traits.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/math/scalar_math.hpp>
#include <eng/core/math/scalar_ops.hpp>

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

/// Tolerancia por modo. Ojo: RETRO16 y RETRO32 comparten `E=12`, asi que su resolucion
/// fraccionaria es la MISMA (1/4096); el ancho de la representacion da rango, no precision.
/// Solo el modo nativo (float) es mucho mas fino.
[[nodiscard]] double tol_for_mode() {
	if (std::strcmp(eng::scalar_mode, "native") == 0) return 1e-4;
	return 5e-2;
}

/// Simulacion escrita SOLO con `eng::real`: oscilador semi-implicito con amortiguacion
/// pequeno, integrado 200 pasos. La referencia es la misma recurrencia en `double`, asi
/// que el error medido es solo de redondeo del escalar.
void test_real() {
	const double k = 0.01;
	const double dt = 0.2;
	const int steps = 200;

	double xr = 1.0, vr = 0.0;
	for (int i = 0; i < steps; ++i) {
		vr -= k * xr;
		xr += dt * vr;
	}

	using R = eng::real;
	R x = scalar_const<R>::from(1.0);
	R v = scalar_const<R>::from(0.0);
	const R kk = scalar_const<R>::from(k);
	const R dtt = scalar_const<R>::from(dt);
	for (int i = 0; i < steps; ++i) {
		v = v - mul_norm(kk, x);
		x = x + mul_norm(dtt, v);
	}

	const double err = rel(numeric_traits<R>::to_double(x), xr);
	std::printf("  oscilador: x=%.6f ref=%.6f err=%.2e (tol %.0e)\n",
		    numeric_traits<R>::to_double(x), xr, err, tol_for_mode());
	check(err < tol_for_mode(), "eng::real: integracion dentro de tolerancia");
}

/// `eng::coord` (coordenada de simulacion, entera en todos los modos) con las operaciones
/// comunes.
void test_coord() {
	const eng::coord a = eng::coord {3};
	const eng::coord b = eng::coord {4};
	const eng::coord sum = a + b;
	check(sum == eng::coord {7}, "eng::coord: 3 + 4 = 7");
	check(a < b, "eng::coord: 3 < 4");
}

/// `eng::intw` (entero de palabra natural) con las operaciones enteras exactas.
void test_intw() {
	using I = eng::intw;
	check(min<I>(I {3}, I {5}) == I {3}, "intw: min");
	check(max<I>(I {3}, I {5}) == I {5}, "intw: max");
	check(clamp(I {7}, I {0}, I {5}) == I {5}, "intw: clamp");
}

} // namespace

int main() {
	std::printf("RealScalar: mode=%s  sizeof(real)=%u sizeof(coord)=%u\n", eng::scalar_mode,
		    static_cast<unsigned>(sizeof(eng::real)),
		    static_cast<unsigned>(sizeof(eng::coord)));
	test_real();
	test_coord();
	test_intw();

	if (g_fail == 0u) {
		std::printf("OK: eng::real/coord/intw (modo %s) validados\n", eng::scalar_mode);
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
