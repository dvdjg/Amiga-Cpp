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
		check(std::fabs(s * s + c * c - 1.0f) <= 6.0e-3, "sin^2+cos^2 = 1 (fixed)");
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

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: fixed_math validado.\n");
	return 0;
}
