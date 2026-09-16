// ============================================================================
// Test HOST-062: primitivas de escalar (min/max/abs/sign/move_towards),
//               easing "back" y Bézier (cuadrática/cúbica).
// ============================================================================
//
// Respalda `eng/core/scalar_ops.hpp`, los easing `_back` de `eng/core/interp.hpp` y
// `bezier2`/`bezier3` de `eng/core/spline.hpp`. Los MISMOS algoritmos con `double`,
// `MiniFloat16` y `q12` (fixed), contra una referencia en `double`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/062_scalar_ops

#include <cmath>
#include <cstdio>

#include <eng/core/minifloat_math.hpp>
#include <eng/core/scalar_ops.hpp>
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

// ---------------------------------------------------------------------------
//  min / max / abs / sign / move_towards
// ---------------------------------------------------------------------------

template <typename S>
void check_ops(const char* tag, float tol) {
	check(em::to_double(em::min(mk<S>(3.0f), mk<S>(-2.0f))) == -2.0, "min devuelve el menor");
	check(em::to_double(em::max(mk<S>(3.0f), mk<S>(-2.0f))) == 3.0, "max devuelve el mayor");
	check(em::to_double(em::abs(mk<S>(-2.5f))) == 2.5, "abs de negativo");
	check(em::to_double(em::abs(mk<S>(2.5f))) == 2.5, "abs de positivo");
	check(em::to_double(em::sign(mk<S>(-1.0f))) == -1.0, "sign de negativo");
	check(em::to_double(em::sign(mk<S>(1.0f))) == 1.0, "sign de positivo");
	check(em::to_double(em::sign(mk<S>(0.0f))) == 0.0, "sign de cero");

	// move_towards: acercamiento por pasos sin pasarse de largo (con clamping exacto).
	// Se usan objetivos dentro de ±8 para que `q12` (fixed 4.12) los represente.
	S v = mk<S>(0.0f);
	for (int i = 0; i < 4; ++i) v = em::move_towards(v, mk<S>(6.0f), mk<S>(2.0f));
	check(std::fabs(em::to_double(v) - 6.0) <= tol, "move_towards clava en el objetivo");
	v = mk<S>(0.0f);
	for (int i = 0; i < 3; ++i) v = em::move_towards(v, mk<S>(-5.0f), mk<S>(2.0f));
	check(std::fabs(em::to_double(v) + 5.0) <= tol, "move_towards clava en el negativo");
	v = mk<S>(0.0f);
	v = em::move_towards(v, mk<S>(6.0f), mk<S>(2.0f));
	check(std::fabs(em::to_double(v) - 2.0) <= tol, "move_towards avanza exactamente el paso");
	std::printf("  %s ops OK (signo final %.2f)\n", tag, em::to_double(em::sign(v)));
}

// ---------------------------------------------------------------------------
//  Easing "back"
// ---------------------------------------------------------------------------

double back_in_ref(double t) {
	return 2.70158 * t * t * t - 1.70158 * t * t;
}
double back_out_ref(double t) {
	const double u = t - 1.0;
	return 1.0 + 2.70158 * u * u * u + 1.70158 * u * u;
}
double back_io_ref(double t) {
	const double c2 = 2.5949095;
	const double s = 2.0 * t;
	if (s < 1.0) return 0.5 * (s * s * ((c2 + 1.0) * s - c2));
	const double u = s - 2.0;
	return 0.5 * (u * u * ((c2 + 1.0) * u + c2) + 2.0);
}

template <typename S>
void check_back(const char* tag, float tol) {
	float mx = 0;
	for (int i = 0; i <= 40; ++i) {
		const S t = mk<S>(static_cast<float>(i) / 40.0f);
		const double td = em::to_double(t);
		mx = std::fmax(mx, std::fabs(em::to_double(em::ease_in_back(t)) - back_in_ref(td)));
		mx = std::fmax(mx, std::fabs(em::to_double(em::ease_out_back(t)) - back_out_ref(td)));
		mx = std::fmax(mx, std::fabs(em::to_double(em::ease_in_out_back(t)) - back_io_ref(td)));
	}
	std::printf("  %s ease_*_back abs max %.2e\n", tag, mx);
	check(mx <= tol, "easing back dentro de tolerancia");
	check(std::fabs(em::to_double(em::ease_in_back(mk<S>(1.0f))) - 1.0) <= tol &&
		      std::fabs(em::to_double(em::ease_out_back(mk<S>(1.0f))) - 1.0) <= tol,
	      "easing back: extremos exactos");
}

// ---------------------------------------------------------------------------
//  Bézier (cuadrática/cúbica)
// ---------------------------------------------------------------------------

/// Referencia en `double` con la base de Bernstein (independiente de de Casteljau).
double bezier3_ref(double p0, double p1, double p2, double p3, double t) {
	const double u = 1.0 - t;
	return u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
}

template <typename S>
void check_bezier(const char* tag, float tol) {
	const S p0 = mk<S>(0.5f), p1 = mk<S>(1.5f), p2 = mk<S>(-0.5f), p3 = mk<S>(2.0f);
	const double d0 = em::to_double(p0), d1 = em::to_double(p1), d2 = em::to_double(p2);
	const double d3 = em::to_double(p3);
	float mx = 0;
	for (int i = 0; i <= 20; ++i) {
		const S t = mk<S>(static_cast<float>(i) / 20.0f);
		mx = std::fmax(mx, std::fabs(em::to_double(em::bezier3(p0, p1, p2, p3, t)) -
					     bezier3_ref(d0, d1, d2, d3, em::to_double(t))));
	}
	std::printf("  %s bezier3 abs max %.2e\n", tag, mx);
	check(mx <= tol, "bezier3 dentro de tolerancia");
	// Los extremos pasan por los puntos de control.
	check(std::fabs(em::to_double(em::bezier3(p0, p1, p2, p3, mk<S>(0.0f))) - d0) <= tol,
	      "bezier3(0) = p0");
	check(std::fabs(em::to_double(em::bezier3(p0, p1, p2, p3, mk<S>(1.0f))) - d3) <= tol,
	      "bezier3(1) = p3");
	check(std::fabs(em::to_double(em::bezier2(p0, p1, p2, mk<S>(0.5f))) -
			(0.25 * d0 + 0.5 * d1 + 0.25 * d2)) <= tol,
	      "bezier2(0.5) = media ponderada");
}

/// Bézier sobre `Vec<2,double>`: coincide componente a componente con la escalar.
void check_vec_bezier() {
	using V2 = em::Vec<2, double>;
	const V2 p0 {0.5, -0.5}, p1 {1.5, 0.0}, p2 {-0.5, 2.0}, p3 {2.0, 1.0};
	float mx = 0;
	for (int i = 0; i <= 20; ++i) {
		const double td = i / 20.0;
		const V2 r = em::bezier3(p0, p1, p2, p3, td);
		mx = std::fmax(mx, std::fabs(r.v[0] - bezier3_ref(p0.v[0], p1.v[0], p2.v[0], p3.v[0], td)));
		mx = std::fmax(mx, std::fabs(r.v[1] - bezier3_ref(p0.v[1], p1.v[1], p2.v[1], p3.v[1], td)));
	}
	std::printf("  Vec<2> bezier3 abs max %.2e\n", mx);
	check(mx <= 1.0e-12f, "bezier3 sobre Vec<2> por componente");
}

} // namespace

int main() {
	std::printf("== HOST-062 scalar_ops + back + bezier ==\n");
	check_ops<double>("double", 1.0e-12f);
	check_ops<MiniFloat16>("MF    ", 1.0e-3f);
	check_ops<er::q12>("q12   ", 1.0e-3f);
	check_back<double>("double", 1.0e-12f);
	check_back<MiniFloat16>("MF    ", 2.0e-2f);
	check_back<er::q12>("q12   ", 2.0e-2f);
	check_bezier<double>("double", 1.0e-12f);
	check_bezier<MiniFloat16>("MF    ", 2.0e-2f);
	check_bezier<er::q12>("q12   ", 2.0e-2f);
	check_vec_bezier();
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: scalar_ops, easing back y Bézier validados.\n");
	return 0;
}
