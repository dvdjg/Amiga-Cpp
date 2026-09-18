// ============================================================================
// Test HOST-104: trigonometría y sqrt para Fixed (eng/core/fixed_math.hpp).
// ============================================================================
//
// Respalda `scalar_sin`/`scalar_cos`/`scalar_sqrt` de `Fixed<s16,E>` (tabla de seno +
// isqrt), y comprueba que `stddev<q12>` y los easings senoidales compilan y son correc-
// tos. Tambien pasa por MiniFloat16 (su trig es la de minifloat_math).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/104_fixed_math

#include <cmath>
#include <cstdio>

#include <eng/core/fixed_math.hpp>
#include <eng/core/geometry.hpp>
#include <eng/core/interp.hpp>
#include <eng/core/minifloat_math.hpp>
#include <eng/core/util/stats.hpp>
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

er::q12 q(float v) {
	return er::q12 {static_cast<eng::s16>(std::lround(v * 4096.0f))};
}

} // namespace

int main() {
	std::printf("== HOST-104 fixed_math ==\n");

	constexpr float kPi = 3.14159265358979f;

	// --- sin/cos de Fixed (tabla) -------------------------------------------
	{
		check(std::fabs(em::to_double(em::scalar_sin<er::q12>::op(q(0.0f)))) <= 3.0e-3,
		      "sin(0) = 0");
		check(std::fabs(em::to_double(em::scalar_sin<er::q12>::op(q(kPi / 2.0f))) - 1.0) <=
			      3.0e-3,
		      "sin(pi/2) = 1");
		check(std::fabs(em::to_double(em::scalar_sin<er::q12>::op(q(kPi)))) <= 3.0e-3,
		      "sin(pi) = 0");
		check(std::fabs(em::to_double(em::scalar_sin<er::q12>::op(q(-kPi / 2.0f))) + 1.0) <=
			      3.0e-3,
		      "sin(-pi/2) = -1");
		check(std::fabs(em::to_double(em::scalar_cos<er::q12>::op(q(0.0f))) - 1.0) <= 3.0e-3,
		      "cos(0) = 1");
		check(std::fabs(em::to_double(em::scalar_cos<er::q12>::op(q(kPi))) + 1.0) <= 3.0e-3,
		      "cos(pi) = -1");

		// Identidad sin^2 + cos^2 = 1 (tolerancia por la tabla).
		const float s = static_cast<float>(em::to_double(em::scalar_sin<er::q12>::op(q(0.7f))));
		const float c = static_cast<float>(em::to_double(em::scalar_cos<er::q12>::op(q(0.7f))));
		check(std::fabs(s * s + c * c - 1.0f) <= 1.5e-2, "sin^2+cos^2 = 1 (fixed)");
	}

	// --- sqrt de Fixed (isqrt) ----------------------------------------------
	{
		check(std::fabs(em::to_double(em::scalar_sqrt<er::q12>::op(q(4.0f))) - 2.0) <= 3.0e-3,
		      "sqrt(4) = 2");
		check(std::fabs(em::to_double(em::scalar_sqrt<er::q12>::op(q(6.25f))) - 2.5) <= 4.0e-3,
		      "sqrt(6.25) = 2.5");
		check(em::scalar_sqrt<er::q12>::op(q(-1.0f)).v == 0, "sqrt(negativo) = 0");
	}

	// --- stddev<q12> ya compila (usa scalar_sqrt<Fixed>) --------------------
	{
		const er::q12 data[4] = {q(1.0f), q(2.0f), q(1.0f), q(2.0f)}; // varianza 0.25
		const eng::Span<const er::q12> xs {data, 4};
		check(std::fabs(em::to_double(eng::util::stddev(xs)) - 0.5) <= 5.0e-3,
		      "stddev<q12> = 0.5");
	}

	// --- easing senoidal con Fixed ------------------------------------------
	{
		check(std::fabs(em::to_double(em::ease_in_sine(q(0.0f)))) <= 6.0e-3, "ease_in_sine(0)=0");
		check(std::fabs(em::to_double(em::ease_out_sine(q(1.0f))) - 1.0) <= 6.0e-3,
		      "ease_out_sine(1)=1");
	}

	// --- MiniFloat16 (trig de minifloat_math) -------------------------------
	{
		const MiniFloat16 half_pi = MiniFloat16(kPi / 2.0f);
		check(std::fabs(em::to_double(em::scalar_sin<MiniFloat16>::op(half_pi)) - 1.0) <= 5.0e-3,
		      "MF sin(pi/2) = 1");
		check(std::fabs(em::to_double(em::scalar_cos<MiniFloat16>::op(MiniFloat16(0.0f))) - 1.0) <=
			      5.0e-3,
		      "MF cos(0) = 1");
	}

	// --- length con Fixed (usa sqrt de fixed_math) --------------------------
	{
		const em::Vec<2, er::q12> v {q(1.5f), q(2.0f)}; // |v| = 2.5, dot = 6.25 (cabe en q12)
		check(std::fabs(em::to_double(em::length(v)) - 2.5) <= 8.0e-3, "length<q12> = 2.5");
		// `normalize` usa `operator/` y con Fixed no compila (el núcleo prohíbe `/`);
		// para fixed se usaría `div_norm` explícito en un futuro refinamiento.
	}

	// --- exp2/log2 de Fixed (tablas) ----------------------------------------
	{
		check(std::fabs(em::to_double(em::scalar_exp2<er::q12>::op(q(0.0f))) - 1.0) <= 4.0e-3,
		      "exp2(0) = 1");
		check(std::fabs(em::to_double(em::scalar_exp2<er::q12>::op(q(1.0f))) - 2.0) <= 8.0e-3,
		      "exp2(1) = 2");
		check(std::fabs(em::to_double(em::scalar_exp2<er::q12>::op(q(-1.0f))) - 0.5) <= 4.0e-3,
		      "exp2(-1) = 0.5");
		check(std::fabs(em::to_double(em::scalar_log2<er::q12>::op(q(1.0f)))) <= 4.0e-3,
		      "log2(1) = 0");
		check(std::fabs(em::to_double(em::scalar_log2<er::q12>::op(q(2.0f))) - 1.0) <= 8.0e-3,
		      "log2(2) = 1");
		check(std::fabs(em::to_double(em::scalar_log2<er::q12>::op(q(4.0f))) - 2.0) <= 8.0e-3,
		      "log2(4) = 2");
	}

	// --- normalize/project/reflect con Fixed (div_norm) ---------------------
	{
		const em::Vec<2, er::q12> v {q(1.5f), q(2.0f)}; // |v| = 2.5
		const em::Vec<2, er::q12> n = em::normalize(v);
		check(std::fabs(em::to_double(n.v[0]) - 0.6) <= 8.0e-3 &&
			      std::fabs(em::to_double(n.v[1]) - 0.8) <= 8.0e-3,
		      "normalize<q12> = (0.6, 0.8)");
		const em::Vec<2, er::q12> onto {q(1.0f), q(0.0f)};
		const em::Vec<2, er::q12> p = em::project(v, onto);
		check(std::fabs(em::to_double(p.v[0]) - 1.5) <= 8.0e-3 &&
			      std::fabs(em::to_double(p.v[1])) <= 8.0e-3,
		      "project<q12>");
		const em::Vec<2, er::q12> up {q(0.0f), q(1.0f)};
		const em::Vec<2, er::q12> r = em::reflect(v, up);
		check(std::fabs(em::to_double(r.v[0]) - 1.5) <= 8.0e-3 &&
			      std::fabs(em::to_double(r.v[1]) + 2.0) <= 8.0e-3,
		      "reflect<q12>");
	}

	// --- smooth_damp con Fixed (usa exp2 fixed) -----------------------------
	{
		const er::q12 out = em::smooth_damp(q(0.0f), q(1.0f), q(1.0f), q(1.0f));
		check(std::fabs(em::to_double(out) - 0.5) <= 1.0e-2, "smooth_damp<q12> = 0.5");
	}

	// --- exp/log/pow de Fixed ------------------------------------------------
	{
		check(std::fabs(em::to_double(em::scalar_exp<er::q12>::op(q(0.0f))) - 1.0) <= 6.0e-3,
		      "exp(0) = 1");
		check(std::fabs(em::to_double(em::scalar_exp<er::q12>::op(q(1.0f))) - 2.71828) <= 1.5e-2,
		      "exp(1) = e");
		check(std::fabs(em::to_double(em::scalar_log<er::q12>::op(q(1.0f)))) <= 1.0e-2,
		      "log(1) = 0");
		// Round-trip: log(exp(0.5)) = 0.5.
		const er::q12 y = em::scalar_exp<er::q12>::op(q(0.5f));
		check(std::fabs(em::to_double(em::scalar_log<er::q12>::op(y)) - 0.5) <= 2.0e-2,
		      "log(exp(0.5)) = 0.5");
		check(std::fabs(em::to_double(em::scalar_pow<er::q12>::op(q(2.0f), q(2.0f))) - 4.0) <=
			      2.0e-2,
		      "pow(2,2) = 4");
		check(std::fabs(em::to_double(em::scalar_pow<er::q12>::op(q(4.0f), q(0.5f))) - 2.0) <=
			      2.0e-2,
		      "pow(4,0.5) = 2");
	}

	// --- tan/atan2/asin/acos de Fixed ---------------------------------------
	{
		check(std::fabs(em::to_double(em::scalar_tan<er::q12>::op(q(0.0f)))) <= 1.0e-2,
		      "tan(0) = 0");
		check(std::fabs(em::to_double(em::scalar_tan<er::q12>::op(q(kPi / 4.0f))) - 1.0) <= 1.5e-2,
		      "tan(pi/4) = 1");
		check(std::fabs(em::to_double(em::scalar_tan<er::q12>::op(q(1.0f))) - 1.5574) <= 3.0e-2,
		      "tan(1) = 1.5574");

		check(std::fabs(em::to_double(em::scalar_atan2<er::q12>::op(q(0.0f), q(1.0f)))) <= 1.5e-2,
		      "atan2(0,1) = 0");
		check(std::fabs(em::to_double(em::scalar_atan2<er::q12>::op(q(1.0f), q(1.0f))) -
				0.7853982) <= 2.5e-2,
		      "atan2(1,1) = pi/4");
		check(std::fabs(em::to_double(em::scalar_atan2<er::q12>::op(q(1.0f), q(0.0f))) -
				1.5707963) <= 2.5e-2,
		      "atan2(1,0) = pi/2");
		check(std::fabs(em::to_double(em::scalar_atan2<er::q12>::op(q(0.0f), q(-1.0f))) -
				3.1415927) <= 2.5e-2,
		      "atan2(0,-1) = pi");
		check(std::fabs(em::to_double(em::scalar_atan2<er::q12>::op(q(-1.0f), q(0.0f))) +
				1.5707963) <= 2.5e-2,
		      "atan2(-1,0) = -pi/2");
		check(std::fabs(em::to_double(em::scalar_atan2<er::q12>::op(q(1.0f), q(-1.0f))) -
				2.3561945) <= 3.0e-2,
		      "atan2(1,-1) = 3pi/4");

		check(std::fabs(em::to_double(em::scalar_asin<er::q12>::op(q(0.5f))) - 0.5235988) <= 3.0e-2,
		      "asin(0.5) = pi/6");
		check(std::fabs(em::to_double(em::scalar_asin<er::q12>::op(q(1.0f))) - 1.5707963) <= 3.0e-2,
		      "asin(1) = pi/2");
		check(std::fabs(em::to_double(em::scalar_acos<er::q12>::op(q(0.5f))) - 1.0471976) <= 3.0e-2,
		      "acos(0.5) = pi/3");
		check(std::fabs(em::to_double(em::scalar_acos<er::q12>::op(q(0.0f))) - 1.5707963) <= 3.0e-2,
		      "acos(0) = pi/2");
	}

	// --- sincos de una sola pasada ------------------------------------------
	{
		const er::q12 x = q(0.7f);
		er::q12 s {0}, c {0};
		em::fixed_sincos<12, 1024>(x, s, c);
		check(s.v == em::fixed_sin<12, 1024>(x).v && c.v == em::fixed_cos<12, 1024>(x).v,
		      "fixed_sincos = fixed_sin/fixed_cos");
		em::scalar_sincos<er::q12>::op(x, s, c);
		check(s.v == em::scalar_sin<er::q12>::op(x).v &&
			      c.v == em::scalar_cos<er::q12>::op(x).v,
		      "scalar_sincos<q12> = scalar_sin/scalar_cos");
	}

	// --- rotate2 por ángulo (sincos en una pasada) --------------------------
	{
		const em::Vec<2, er::q12> v {q(1.0f), q(0.0f)};
		const em::Vec<2, er::q12> r = em::rotate2(v, q(kPi / 2.0f)); // (0, 1)
		check(std::fabs(em::to_double(r.v[0])) <= 8.0e-3 &&
			      std::fabs(em::to_double(r.v[1]) - 1.0) <= 8.0e-3,
		      "rotate2<q12>(ángulo pi/2) = (0,1)");
	}

	// --- wrap_angle / angle_diff de Fixed -----------------------------------
	{
		const float two_pi = 2.0f * kPi;
		check(std::fabs(em::to_double(em::wrap_angle(q(0.5f))) - 0.5) <= 1.0e-2,
		      "wrap_angle(0.5) = 0.5");
		check(std::fabs(em::to_double(em::wrap_angle(q(two_pi + 0.5f))) - 0.5) <= 1.5e-2,
		      "wrap_angle(2pi+0.5) = 0.5");
		check(std::fabs(em::to_double(em::wrap_angle(q(kPi + 0.1f))) - (-kPi + 0.1f)) <= 1.5e-2,
		      "wrap_angle(pi+0.1) = -pi+0.1");
		check(std::fabs(em::to_double(em::angle_diff(q(0.1f), q(two_pi - 0.1f))) - 0.2f) <= 1.5e-2,
		      "angle_diff(0.1, 2pi-0.1) = 0.2");
	}

	// --- tamaño de tabla elegible en compilación ----------------------------
	{
		const er::q12 x {2300}; // ≈ 0.5615 rad
		const double ref = std::sin(2300.0 / 4096.0);
		const double e_def = std::fabs(em::to_double(em::fixed_sin<12, 1024>(x)) - ref);
		const double e_lo = std::fabs(em::to_double(em::fixed_sin<12, 256>(x)) - ref);
		check(e_def <= 4.0e-3, "fixed_sin<1024> preciso");
		check(e_lo <= 3.0e-2, "fixed_sin<256> aceptable");
		// exp2/log2 con tablas pequeñas.
		check(std::fabs(em::to_double(em::fixed_exp2<12, 64>(q(1.0f))) - 2.0) <= 2.0e-2,
		      "fixed_exp2<64>(1) ≈ 2");
		check(std::fabs(em::to_double(em::fixed_log2<12, 64>(q(4.0f))) - 2.0) <= 2.0e-2,
		      "fixed_log2<64>(4) ≈ 2");
		std::printf("  sin err: 1024=%.4f 256=%.4f\n", e_def, e_lo);
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: fixed_math validado.\n");
	return 0;
}
