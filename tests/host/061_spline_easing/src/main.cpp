// ============================================================================
// Test HOST-061: splines (Hermite/Catmull-Rom) y easing genéricos.
// ============================================================================
//
// Respalda `eng/core/spline.hpp` y los easing de `eng/core/interp.hpp` sobre el escalar:
// los MISMOS algoritmos con `double`, `MiniFloat16` y `q12` (fixed), comparando contra
// una referencia en `double` calculada desde las MISMAS entradas redondeadas.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/061_spline_easing

#include <cmath>
#include <cstdio>
#include <type_traits>

#include <eng/core/interp.hpp>
#include <eng/core/spline.hpp>
#include <eng/retro/fixed_q.hpp>

using eng::math::MiniFloat16;
namespace em = eng::math;
namespace er = eng::retro;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Construye un escalar desde un `float` (para fixed, cuantiza a 4.12).
template <typename S>
S mk(float x) {
	return S(x);
}
template <>
er::q12 mk<er::q12>(float x) {
	return er::q12 {static_cast<eng::s16>(std::lround(x * 4096.0f))};
}

double hermite_ref(double p0, double m0, double p1, double m1, double t) {
	const double t2 = t * t, t3 = t2 * t;
	return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * p1 +
	       (t3 - t2) * m1;
}
double catmull_ref(double p0, double p1, double p2, double p3, double t) {
	return hermite_ref(p1, (p2 - p0) / 2.0, p2, (p3 - p1) / 2.0, t);
}

template <typename S>
void check_curves(const char* tag, float tol) {
	const S p0 = mk<S>(0.5f), p1 = mk<S>(1.5f), p2 = mk<S>(-0.5f), p3 = mk<S>(2.0f);
	const S m0 = mk<S>(1.0f), m1 = mk<S>(-0.5f);
	const double d0 = em::to_double(p0), d1 = em::to_double(p1), d2 = em::to_double(p2);
	const double d3 = em::to_double(p3), dm0 = em::to_double(m0), dm1 = em::to_double(m1);

	float eh = 0, ec = 0;
	for (int i = 0; i <= 20; ++i) {
		const S t = mk<S>(static_cast<float>(i) / 20.0f);
		const double td = em::to_double(t);
		eh = std::fmax(eh, std::fabs(em::to_double(em::hermite(p0, m0, p1, m1, t)) -
					     hermite_ref(d0, dm0, d1, dm1, td)));
		ec = std::fmax(ec, std::fabs(em::to_double(em::catmull_rom(p0, p1, p2, p3, t)) -
					     catmull_ref(d0, d1, d2, d3, td)));
	}
	// paso por los puntos de control
	check(std::fabs(em::to_double(em::catmull_rom(p0, p1, p2, p3, mk<S>(0.0f))) - d1) <= tol,
	      "catmull_rom(0) = p1");
	check(std::fabs(em::to_double(em::catmull_rom(p0, p1, p2, p3, mk<S>(1.0f))) - d2) <= tol,
	      "catmull_rom(1) = p2");
	std::printf("  %s hermite %.2e  catmull_rom %.2e\n", tag, eh, ec);
	check(eh <= tol, "hermite dentro de tolerancia");
	check(ec <= tol, "catmull_rom dentro de tolerancia");
}

template <typename S>
void check_easing(const char* tag, float tol) {
	float e_in = 0, e_out = 0, e_io = 0, e_ci = 0, e_co = 0, e_cio = 0;
	for (int i = 0; i <= 20; ++i) {
		const S t = mk<S>(static_cast<float>(i) / 20.0f);
		const double td = em::to_double(t);
		const double in = td * td;
		const double out = td * (2.0 - td);
		const double io = td < 0.5 ? 2.0 * td * td : 1.0 - 2.0 * (1.0 - td) * (1.0 - td);
		const double ci = td * td * td;
		const double co = 1.0 - (1.0 - td) * (1.0 - td) * (1.0 - td);
		const double cio = td < 0.5 ? 4.0 * td * td * td
					    : 1.0 - 4.0 * (1.0 - td) * (1.0 - td) * (1.0 - td);
		e_in = std::fmax(e_in, std::fabs(em::to_double(em::ease_in_quad(t)) - in));
		e_out = std::fmax(e_out, std::fabs(em::to_double(em::ease_out_quad(t)) - out));
		e_io = std::fmax(e_io, std::fabs(em::to_double(em::ease_in_out_quad(t)) - io));
		e_ci = std::fmax(e_ci, std::fabs(em::to_double(em::ease_in_cubic(t)) - ci));
		e_co = std::fmax(e_co, std::fabs(em::to_double(em::ease_out_cubic(t)) - co));
		e_cio = std::fmax(e_cio, std::fabs(em::to_double(em::ease_in_out_cubic(t)) - cio));
	}
	const float mx = std::fmax(std::fmax(std::fmax(e_in, e_out), std::fmax(e_io, e_ci)),
				   std::fmax(e_co, e_cio));
	std::printf("  %s easing abs max %.2e\n", tag, mx);
	check(mx <= tol, "easing dentro de tolerancia");
	// extremos: 0 -> 0, 1 -> 1
	check(em::to_double(em::ease_in_cubic(mk<S>(0.0f))) == 0.0 &&
		      std::fabs(em::to_double(em::ease_out_cubic(mk<S>(1.0f))) - 1.0) <= tol &&
		      std::fabs(em::to_double(em::ease_in_out_cubic(mk<S>(1.0f))) - 1.0) <= tol,
	      "easing: extremos exactos");
}

} // namespace

int main() {
	std::printf("== HOST-061 splines + easing ==\n");
	check_curves<double>("double", 1.0e-12f);
	check_curves<MiniFloat16>("MF    ", 1.5e-2f);
	check_curves<er::q12>("q12   ", 1.5e-2f);
	check_easing<double>("double", 1.0e-12f);
	check_easing<MiniFloat16>("MF    ", 1.0e-2f);
	check_easing<er::q12>("q12   ", 1.0e-2f);
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: splines (Hermite/Catmull-Rom) y easing genéricos validados.\n");
	return 0;
}
