// ============================================================================
// Test HOST-057: matemáticas clásicas sobre MiniFloat16.
// ============================================================================
//
// Respalda `eng/core/math/minifloat_math.hpp`: sqrt, exp, log, pow y trigonometría
// (sin/cos/tan) implementadas solo con aritmética de 16 bits (sin float, sin libgcc).
// Se comparan contra `std::sqrt/exp/log/pow/sin/cos/tan` sobre ENTRADAS IDÉNTICAS (las
// ya redondeadas a MiniFloat16) y se fijan las tolerancias a partir de lo medido.
//
// Precisión observada (10 bits de mantisa ≈ 5e-4 por operación):
//   sqrt  ~1e-3 rel   exp ~6e-4 rel   log ~1.5e-3 rel (o ~1e-2 abs en el extremo)
//   sin/cos ~2e-3 abs   tan ~1e-2 rel (crece cerca de los polos)
//   pow ~1e-2 rel (crece con |e·log(base)|, que es inherente a exp(e·log base))
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/057_minifloat16_math

#include <cmath>
#include <cstdio>

#include <eng/core/math/minifloat_math.hpp>

using eng::math::MiniFloat16;
namespace m = eng::math;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

float rel_err(float got, float want) {
	const float d = std::fabs(want);
	return d < 1.0e-12f ? std::fabs(got - want) : std::fabs(got - want) / d;
}

bool in_range(float want) { return std::isfinite(want) && want >= 6.2e-5f && want <= 65400.0f; }

// ---------------------------------------------------------------------------
//  1. Valores simples y exactos
// ---------------------------------------------------------------------------

void test_simple_values() {
	// sqrt
	check(m::sqrt(MiniFloat16(4.0f)).raw == MiniFloat16(2.0f).raw, "sqrt(4) = 2");
	check(m::sqrt(MiniFloat16(1.0f)).raw == MiniFloat16(1.0f).raw, "sqrt(1) = 1");
	check(rel_err(static_cast<float>(m::sqrt(MiniFloat16(2.0f))), std::sqrt(2.0f)) < 3.0e-3f,
	      "sqrt(2) ~ 1.414");
	check(rel_err(static_cast<float>(m::sqrt(MiniFloat16(0.25f))), 0.5f) < 3.0e-3f, "sqrt(0.25) = 0.5");

	// exp
	check(m::exp(MiniFloat16(0.0f)).raw == MiniFloat16(1.0f).raw, "exp(0) = 1");
	check(rel_err(static_cast<float>(m::exp(MiniFloat16(1.0f))), std::exp(1.0f)) < 2.0e-3f,
	      "exp(1) ~ e");
	check(rel_err(static_cast<float>(m::exp(MiniFloat16(-1.0f))), std::exp(-1.0f)) < 2.0e-3f,
	      "exp(-1) ~ 1/e");

	// exp2 / pow2 (2^x)
	check(m::exp2(MiniFloat16(0.0f)).raw == MiniFloat16(1.0f).raw, "exp2(0) = 1");
	check(m::exp2(MiniFloat16(10.0f)).raw == MiniFloat16(1024.0f).raw, "exp2(10) = 1024 exacto");
	check(m::exp2(MiniFloat16(-1.0f)).raw == MiniFloat16(0.5f).raw, "exp2(-1) = 0.5");
	check(rel_err(static_cast<float>(m::exp2(MiniFloat16(0.5f))), std::sqrt(2.0f)) < 2.0e-3f,
	      "exp2(0.5) ~ sqrt(2)");
	check(m::pow2(MiniFloat16(3.0f)).raw == MiniFloat16(8.0f).raw, "pow2(3) = 8");

	// log
	check(m::log(MiniFloat16(1.0f)).is_zero(), "log(1) = 0");
	check(std::fabs(static_cast<float>(m::log(MiniFloat16(2.7182818f))) - 1.0f) < 3.0e-3f,
	      "log(e) ~ 1");
	check(std::fabs(static_cast<float>(m::log(MiniFloat16(2.0f))) - std::log(2.0f)) < 2.0e-3f,
	      "log(2) ~ 0.693");
	check(m::log(MiniFloat16(0.0f)).is_inf() &&
		      (m::log(MiniFloat16(0.0f)).raw & MiniFloat16::sign_mask) != 0u,
	      "log(0) = -inf");

	// pow
	check(rel_err(static_cast<float>(m::pow(MiniFloat16(2.0f), MiniFloat16(10.0f))), 1024.0f) < 3.0e-2f,
	      "pow(2,10) ~ 1024");
	check(rel_err(static_cast<float>(m::pow(MiniFloat16(4.0f), MiniFloat16(0.5f))), 2.0f) < 3.0e-2f,
	      "pow(4,0.5) ~ 2");
	check(m::pow(MiniFloat16(2.0f), MiniFloat16(0.0f)).raw == MiniFloat16(1.0f).raw, "pow(a,0) = 1");

	// trig
	check(m::sin(MiniFloat16(0.0f)).raw == 0u, "sin(0) = 0");
	check(m::cos(MiniFloat16(0.0f)).raw == MiniFloat16(1.0f).raw, "cos(0) = 1");
	check(std::fabs(static_cast<float>(m::sin(MiniFloat16(1.5707963f))) - 1.0f) < 3.0e-3f,
	      "sin(pi/2) ~ 1");
	check(std::fabs(static_cast<float>(m::cos(MiniFloat16(3.1415927f))) + 1.0f) < 3.0e-3f,
	      "cos(pi) ~ -1");
	check(std::fabs(static_cast<float>(m::sin(MiniFloat16(3.1415927f)))) < 3.0e-3f, "sin(pi) ~ 0");
	check(rel_err(static_cast<float>(m::tan(MiniFloat16(0.7853982f))), 1.0f) < 3.0e-2f, "tan(pi/4) ~ 1");

	// inversas
	check(m::atan(MiniFloat16(0.0f)).is_zero(), "atan(0) = 0");
	check(std::fabs(static_cast<float>(m::atan(MiniFloat16(1.0f))) - 0.7853982f) < 3.0e-3f,
	      "atan(1) ~ pi/4");
	check(std::fabs(static_cast<float>(m::atan2(MiniFloat16(1.0f), MiniFloat16(-1.0f))) - 2.3561945f) <
		      4.0e-3f,
	      "atan2(1,-1) ~ 3pi/4");
	check(m::asin(MiniFloat16(0.0f)).is_zero(), "asin(0) = 0");
	check(std::fabs(static_cast<float>(m::asin(MiniFloat16(1.0f))) - 1.5707963f) < 3.0e-3f,
	      "asin(1) ~ pi/2");
	check(m::acos(MiniFloat16(1.0f)).is_zero(), "acos(1) = 0");
	check(std::fabs(static_cast<float>(m::acos(MiniFloat16(-1.0f))) - 3.1415927f) < 4.0e-3f,
	      "acos(-1) ~ pi");

	// pow entero: camino exacto (cuadrado y multiplicación) y base negativa
	check(m::pow(MiniFloat16(2.0f), MiniFloat16(10.0f)).raw == MiniFloat16(1024.0f).raw, "pow(2,10) = 1024 exacto");
	check(m::pow(MiniFloat16(-2.0f), MiniFloat16(3.0f)).raw == MiniFloat16(-8.0f).raw, "pow(-2,3) = -8 exacto");
	check(m::pow(MiniFloat16(-2.0f), MiniFloat16(4.0f)).raw == MiniFloat16(16.0f).raw, "pow(-2,4) = 16 exacto");
	check(rel_err(static_cast<float>(m::pow(MiniFloat16(3.0f), MiniFloat16(-2.0f))), 1.0f / 9.0f) <
		      3.0e-3f,
	      "pow(3,-2) ~ 1/9");

	// logaritmos en otras bases, hypot y sincos
	check(m::log2(MiniFloat16(8.0f)).raw == MiniFloat16(3.0f).raw, "log2(8) = 3 exacto");
	check(m::log2(MiniFloat16(1024.0f)).raw == MiniFloat16(10.0f).raw, "log2(1024) = 10 exacto");
	check(rel_err(static_cast<float>(m::log10(MiniFloat16(1000.0f))), 3.0f) < 5.0e-3f,
	      "log10(1000) ~ 3");
	check(rel_err(static_cast<float>(m::hypot(MiniFloat16(3.0f), MiniFloat16(4.0f))), 5.0f) <
		      3.0e-3f,
	      "hypot(3,4) ~ 5");
	{
		MiniFloat16 ss, cc;
		m::sincos(MiniFloat16(1.0f), ss, cc);
		check(std::fabs(static_cast<float>(ss) - std::sin(1.0f)) < 3.0e-3f &&
			      std::fabs(static_cast<float>(cc) - std::cos(1.0f)) < 3.0e-3f,
		      "sincos(1) ~ (sin, cos)");
	}
}

// ---------------------------------------------------------------------------
//  2. Barridos contra float
// ---------------------------------------------------------------------------

void test_sweeps() {
	const float mags[] = {1.0f, 1.1f, 1.3f, 1.5f, 1.7f, 1.9f};

	// sqrt sobre todo el rango finito
	{
		float mx = 0;
		for (int e = -14; e <= 15; ++e)
			for (float mg : mags) {
				const float v = std::ldexp(mg, e);
				if (v > 65504.0f) continue;
				const MiniFloat16 x(v);
				mx = std::fmax(mx, rel_err(static_cast<float>(m::sqrt(x)),
							   std::sqrt(static_cast<float>(x))));
			}
		std::printf("  sqrt rel max %.2e\n", mx);
		check(mx <= 3.0e-3f, "sqrt: rel <= 3e-3");
	}

	// exp sobre [-11, 11] (dentro del rango finito)
	{
		float mx = 0;
		for (int i = -220; i <= 220; ++i) {
			const MiniFloat16 x(i * 0.05f);
			const float want = std::exp(static_cast<float>(x));
			if (!in_range(want)) continue;
			mx = std::fmax(mx, rel_err(static_cast<float>(m::exp(x)), want));
		}
		std::printf("  exp  rel max %.2e\n", mx);
		check(mx <= 2.0e-3f, "exp: rel <= 2e-3");
	}

	// exp2 (2^x) sobre el rango finito
	{
		float mx = 0;
		for (int i = -150; i <= 160; ++i) {
			const MiniFloat16 x(i * 0.1f);
			const float want = std::exp2(static_cast<float>(x));
			if (!in_range(want)) continue;
			mx = std::fmax(mx, rel_err(static_cast<float>(m::exp2(x)), want));
		}
		std::printf("  exp2 rel max %.2e\n", mx);
		check(mx <= 2.0e-3f, "exp2: rel <= 2e-3");
	}

	// log: error absoluto (log(1)=0 hace inútil el relativo) y relativo lejos de 1
	{
		float mx_abs = 0, mx_rel = 0;
		for (int e = -14; e <= 15; ++e)
			for (float mg : mags) {
				const float v = std::ldexp(mg, e);
				if (v > 65504.0f || v < 6.2e-5f) continue;
				const MiniFloat16 x(v);
				const float want = std::log(static_cast<float>(x));
				const float got = static_cast<float>(m::log(x));
				mx_abs = std::fmax(mx_abs, std::fabs(got - want));
				if (std::fabs(want) > 0.5f) mx_rel = std::fmax(mx_rel, rel_err(got, want));
			}
		std::printf("  log  abs max %.2e   rel (|log|>0.5) %.2e\n", mx_abs, mx_rel);
		check(mx_abs <= 2.5e-2f, "log: abs <= 2.5e-2");
		check(mx_rel <= 5.0e-3f, "log: rel <= 5e-3");
	}

	// pow
	{
		float mx = 0;
		for (int i = 0; i <= 40; ++i)
			for (int j = -16; j <= 16; ++j) {
				const MiniFloat16 B(0.1f + i * 0.25f);
				const MiniFloat16 E(j * 0.25f);
				const float want = std::pow(static_cast<float>(B), static_cast<float>(E));
				if (!in_range(want)) continue;
				mx = std::fmax(mx, rel_err(static_cast<float>(m::pow(B, E)), want));
			}
		std::printf("  pow  rel max %.2e\n", mx);
		check(mx <= 3.0e-2f, "pow: rel <= 3e-2");
	}

	// trig en [-2pi, 2pi]
	{
		float mxs = 0, mxc = 0, mxt = 0;
		for (int i = -314; i <= 314; ++i) {
			const MiniFloat16 x(i * 0.02f);
			const float xm = static_cast<float>(x);
			mxs = std::fmax(mxs, std::fabs(static_cast<float>(m::sin(x)) - std::sin(xm)));
			mxc = std::fmax(mxc, std::fabs(static_cast<float>(m::cos(x)) - std::cos(xm)));
			if (std::fabs(std::cos(xm)) > 0.1f)
				mxt = std::fmax(mxt, rel_err(static_cast<float>(m::tan(x)), std::tan(xm)));
		}
		std::printf("  sin  abs max %.2e   cos abs max %.2e   tan rel max %.2e\n", mxs, mxc, mxt);
		check(mxs <= 3.0e-3f, "sin: abs <= 3e-3");
		check(mxc <= 3.0e-3f, "cos: abs <= 3e-3");
		check(mxt <= 3.0e-2f, "tan: rel <= 3e-2 (|cos| > 0.1)");
	}

	// log2/log10/hypot
	{
		float m2 = 0, m10 = 0, mh = 0;
		for (int e = -14; e <= 15; ++e)
			for (float mg : mags) {
				const float v = std::ldexp(mg, e);
				if (v > 65504.0f || v < 6.2e-5f) continue;
				const MiniFloat16 x(v);
				const float xm = static_cast<float>(x);
				m2 = std::fmax(m2, std::fabs(static_cast<float>(m::log2(x)) - std::log2(xm)));
				m10 = std::fmax(m10, std::fabs(static_cast<float>(m::log10(x)) - std::log10(xm)));
			}
		for (int i = -60; i <= 60; ++i)
			for (int j = -60; j <= 60; ++j) {
				const MiniFloat16 x(i * 0.7f), y(j * 1.3f);
				const float want = std::hypot(static_cast<float>(x), static_cast<float>(y));
				if (want < 6.2e-5f || want > 65400.0f) continue;
				mh = std::fmax(mh, rel_err(static_cast<float>(m::hypot(x, y)), want));
			}
		std::printf("  log2 abs max %.2e   log10 abs max %.2e   hypot rel max %.2e\n", m2, m10, mh);
		check(m2 <= 1.2e-2f, "log2: abs <= 1.2e-2");
		check(m10 <= 8.0e-3f, "log10: abs <= 8e-3");
		check(mh <= 4.0e-3f, "hypot: rel <= 4e-3");
	}

	// inversas
	{
		float mxa = 0, mxt2 = 0, mxs = 0, mxc = 0;
		for (int i = -2000; i <= 2000; ++i) {
			const MiniFloat16 x(i * 0.005f);
			mxa = std::fmax(mxa, std::fabs(static_cast<float>(m::atan(x)) -
						       std::atan(static_cast<float>(x))));
		}
		for (int i = -100; i <= 100; ++i)
			for (int j = -100; j <= 100; ++j) {
				if (i == 0 && j == 0) continue;
				const MiniFloat16 y(i * 0.05f), x(j * 0.05f);
				mxt2 = std::fmax(mxt2, std::fabs(static_cast<float>(m::atan2(y, x)) -
								 std::atan2(static_cast<float>(y),
									    static_cast<float>(x))));
			}
		for (int i = -100; i <= 100; ++i) {
			const MiniFloat16 x(i * 0.01f);
			mxs = std::fmax(mxs, std::fabs(static_cast<float>(m::asin(x)) -
						       std::asin(static_cast<float>(x))));
			mxc = std::fmax(mxc, std::fabs(static_cast<float>(m::acos(x)) -
						       std::acos(static_cast<float>(x))));
		}
		std::printf("  atan abs max %.2e   atan2 abs max %.2e   asin abs %.2e   acos abs %.2e\n", mxa,
			    mxt2, mxs, mxc);
		check(mxa <= 3.0e-3f, "atan: abs <= 3e-3");
		check(mxt2 <= 4.0e-3f, "atan2: abs <= 4e-3");
		check(mxs <= 3.0e-3f, "asin: abs <= 3e-3");
		check(mxc <= 3.0e-3f, "acos: abs <= 3e-3");
	}
}

// ---------------------------------------------------------------------------
//  3. Fronteras y dominios inválidos
// ---------------------------------------------------------------------------

void test_edges() {
	// sqrt
	check(m::sqrt(MiniFloat16(0.0f)).is_zero(), "sqrt(0) = 0");
	check(m::sqrt(MiniFloat16(-4.0f)).is_inf(), "sqrt(negativo) -> inf (indefinido)");
	check(m::sqrt(MiniFloat16::from_raw(MiniFloat16::exp_mask)).is_inf(), "sqrt(inf) = inf");

	// exp
	check(m::exp(MiniFloat16(20.0f)).is_inf(), "exp(20) satura a inf");
	check(m::exp(MiniFloat16(-20.0f)).is_zero(), "exp(-20) satura a 0");
	check(m::exp(MiniFloat16::from_raw(MiniFloat16::sign_mask | MiniFloat16::exp_mask)).is_zero(),
	      "exp(-inf) = 0");

	// log / pow dominios
	check(m::log(MiniFloat16(-2.0f)).is_inf() &&
		      (m::log(MiniFloat16(-2.0f)).raw & MiniFloat16::sign_mask) == 0u,
	      "log(negativo) -> +inf");
	check(m::pow(MiniFloat16(0.0f), MiniFloat16(2.0f)).is_zero(), "pow(0,+) = 0");
	check(m::pow(MiniFloat16(0.0f), MiniFloat16(0.0f)).raw == MiniFloat16(1.0f).raw, "pow(0,0) = 1");
	check(m::pow(MiniFloat16(0.0f), MiniFloat16(-2.0f)).is_inf(), "pow(0,-) = inf");
	check(m::pow(MiniFloat16(-2.0f), MiniFloat16(1.5f)).is_inf(),
	      "pow(base negativa, exponente no entero) -> inf (indefinido)");

	// tan cerca del polo
	const MiniFloat16 near_pole(1.5707963f);
	check(m::tan(near_pole).is_inf() || std::fabs(static_cast<float>(m::tan(near_pole))) > 100.0f,
	      "tan(pi/2) satura o es enorme");

	// inversas fuera de dominio
	check(m::asin(MiniFloat16(2.0f)).is_inf(), "asin(2) -> inf (indefinido)");
	check(m::acos(MiniFloat16(-2.0f)).is_inf(), "acos(-2) -> inf (indefinido)");
	check(std::fabs(static_cast<float>(m::atan(MiniFloat16::from_raw(MiniFloat16::exp_mask))) -
			1.5707963f) < 3.0e-3f,
	      "atan(+inf) = pi/2");

	// hypot
	check(m::hypot(MiniFloat16(0.0f), MiniFloat16(0.0f)).is_zero(), "hypot(0,0) = 0");
	check(m::hypot(MiniFloat16::from_raw(MiniFloat16::exp_mask), MiniFloat16(1.0f)).is_inf(),
	      "hypot(inf,1) = inf");
}

// ---------------------------------------------------------------------------
//  4. Identidades de composición (red de seguridad: cazan errores que un barrido
//     función a función puede dejar pasar)
// ---------------------------------------------------------------------------

void test_identities() {
	const float mags[] = {1.0f, 1.1f, 1.3f, 1.5f, 1.7f, 1.9f};

	// sin² + cos² = 1
	{
		float mx = 0;
		for (int i = -314; i <= 314; ++i) {
			const MiniFloat16 x(i * 0.02f);
			MiniFloat16 s, c;
			m::sincos(x, s, c);
			const float fs = static_cast<float>(s), fc = static_cast<float>(c);
			mx = std::fmax(mx, std::fabs(fs * fs + fc * fc - 1.0f));
		}
		std::printf("  identidad sin2+cos2-1 abs max %.2e\n", mx);
		check(mx <= 5.0e-3f, "sin²+cos² = 1 (abs <= 5e-3)");
	}

	// exp(log(x)) = x
	{
		float mx = 0;
		for (int e = -14; e <= 15; ++e)
			for (float mg : mags) {
				const float v = std::ldexp(mg, e);
				if (v > 65504.0f || v < 6.2e-5f) continue;
				const MiniFloat16 x(v);
				mx = std::fmax(mx, rel_err(static_cast<float>(m::exp(m::log(x))),
							   static_cast<float>(x)));
			}
		std::printf("  identidad exp(log x) rel max %.2e\n", mx);
		check(mx <= 3.0e-2f, "exp(log x) = x (rel <= 3e-2)");
	}

	// log(exp(x)) = x
	{
		float mx = 0;
		for (int i = -90; i <= 110; ++i) {
			const MiniFloat16 x(i * 0.1f);
			const float want = static_cast<float>(x);
			const float got = static_cast<float>(m::log(m::exp(x)));
			if (!std::isfinite(got)) continue;
			mx = std::fmax(mx, std::fabs(got - want));
		}
		std::printf("  identidad log(exp x)-x abs max %.2e\n", mx);
		check(mx <= 1.2e-2f, "log(exp x) = x (abs <= 1.2e-2)");
	}

	// sqrt(x)² = x  y  pow(x,2) = x·x (camino entero exacto)
	{
		float mx = 0;
		bool pow_exact = true;
		for (int e = -14; e <= 15; ++e)
			for (float mg : mags) {
				const float v = std::ldexp(mg, e);
				if (v > 65504.0f || v < 6.2e-5f) continue;
				const MiniFloat16 x(v);
				const MiniFloat16 r = m::sqrt(x);
				mx = std::fmax(mx, rel_err(static_cast<float>(r * r), static_cast<float>(x)));
				if ((m::pow(x, MiniFloat16(2.0f)).raw) != (x * x).raw) pow_exact = false;
			}
		std::printf("  identidad sqrt(x)^2 rel max %.2e ; pow(x,2)==x*x %s\n", mx,
			    pow_exact ? "si" : "NO");
		check(mx <= 4.0e-3f, "sqrt(x)² = x (rel <= 4e-3)");
		check(pow_exact, "pow(x,2) = x·x exacto");
	}

	// sin(asin(x)) = x  y  atan(tan(x)) = x
	{
		float mxs = 0, mxa = 0;
		for (int i = -100; i <= 100; ++i) {
			const MiniFloat16 x(i * 0.01f);
			mxs = std::fmax(mxs, std::fabs(static_cast<float>(m::sin(m::asin(x))) -
						       static_cast<float>(x)));
		}
		for (int i = -120; i <= 120; ++i) {
			const MiniFloat16 x(i * 0.01f); // |x| <= 1.2, lejos del polo
			mxa = std::fmax(mxa, std::fabs(static_cast<float>(m::atan(m::tan(x))) -
						       static_cast<float>(x)));
		}
		std::printf("  identidad sin(asin x)-x abs %.2e ; atan(tan x)-x abs %.2e\n", mxs, mxa);
		check(mxs <= 5.0e-3f, "sin(asin x) = x (abs <= 5e-3)");
		check(mxa <= 1.0e-2f, "atan(tan x) = x (abs <= 1e-2, |x| <= 1.2)");
	}

	// hypot(x,0) = |x|
	{
		bool ok = true;
		for (int e = -14; e <= 15; ++e) {
			const MiniFloat16 x(std::ldexp(1.3f, e));
			if (static_cast<float>(x) > 65504.0f) continue;
			if (m::hypot(x, MiniFloat16(0.0f)).raw != x.raw) ok = false;
			if (m::hypot(MiniFloat16(0.0f), x).raw != x.raw) ok = false;
		}
		check(ok, "hypot(x,0) = |x| exacto");
	}
}

// ---------------------------------------------------------------------------
//  5. FMA (`mul_add`/`mac`) y ángulos (`wrap_angle`/`angle_diff`)
// ---------------------------------------------------------------------------

void test_mul_add_and_angles() {
	// exactos
	check(m::mul_add(MiniFloat16(2.0f), MiniFloat16(3.0f), MiniFloat16(1.0f)).raw ==
		      MiniFloat16(7.0f).raw,
	      "mul_add(2,3,1) = 7");
	check(m::mac(MiniFloat16(1.5f), MiniFloat16(2.0f), MiniFloat16(0.25f)).raw == MiniFloat16(3.25f).raw,
	      "mac(1.5,2,0.25) = 3.25");
	check(m::mul_add(MiniFloat16(1.0f), MiniFloat16(1.0f), MiniFloat16(-1.0f)).is_zero(),
	      "mul_add(1,1,-1) = 0");
	check(m::mul_add(MiniFloat16(0.0f), MiniFloat16(5.0f), MiniFloat16(1.25f)).raw ==
		      MiniFloat16(1.25f).raw,
	      "mul_add(0,b,c) = c");

	// el FMA no debe ser PEOR que el no fusionado (2 redondeos); error relativo
	float fma_rel = 0, plain_rel = 0;
	const float vals[] = {1.1f, 1.7f, 2.3f, 3.1f, 0.9f, -1.3f, 4.2f, -2.7f};
	for (float a : vals)
		for (float b : vals)
			for (float c : vals) {
				const MiniFloat16 A(a), B(b), C(c);
				const double prod = static_cast<double>(static_cast<float>(A)) *
						    static_cast<double>(static_cast<float>(B));
				const double want = prod + static_cast<double>(static_cast<float>(C));
				// sin cancelación (si no, el error relativo no acota nada)
				if (std::fabs(want) < 0.25 * std::fmax(std::fabs(prod), std::fabs(static_cast<double>(static_cast<float>(C)))))
					continue;
				const double ef = std::fabs(static_cast<double>(static_cast<float>(
								       m::mul_add(A, B, C))) - want) /
						  std::fabs(want);
				const double ep = std::fabs(static_cast<double>(static_cast<float>(A * B + C)) -
							    want) /
						  std::fabs(want);
				fma_rel = static_cast<float>(std::fmax(fma_rel, ef));
				plain_rel = static_cast<float>(std::fmax(plain_rel, ep));
			}
	std::printf("  FMA rel max %.2e   sin fusionar rel max %.2e\n", fma_rel, plain_rel);
	check(fma_rel <= plain_rel + 1.0e-6f, "mul_add (1 redondeo) no es peor que a*b+c (2)");
	check(fma_rel <= 2.0e-3f, "mul_add: error relativo <= 2e-3");

	// wrap_angle / angle_diff
	const float pi = 3.1415927f;
	check(std::fabs(static_cast<float>(m::wrap_angle(MiniFloat16(0.5f))) - 0.5f) < 3.0e-3f,
	      "wrap_angle(0.5) = 0.5");
	check(std::fabs(static_cast<float>(m::wrap_angle(MiniFloat16(2.0f * pi + 0.5f))) - 0.5f) < 5.0e-3f,
	      "wrap_angle(2pi+0.5) = 0.5");
	check(std::fabs(static_cast<float>(m::wrap_angle(MiniFloat16(pi + 0.1f))) - (-pi + 0.1f)) <
		      5.0e-3f,
	      "wrap_angle(pi+0.1) = -pi+0.1");
	check(std::fabs(static_cast<float>(m::angle_diff(MiniFloat16(0.1f), MiniFloat16(2.0f * pi - 0.1f))) -
			0.2f) < 6.0e-3f,
	      "angle_diff(0.1, 2pi-0.1) = 0.2");
}

} // namespace

int main() {
	std::printf("== HOST-057 MiniFloat16 math ==\n");
	test_simple_values();
	test_sweeps();
	test_edges();
	test_identities();
	test_mul_add_and_angles();
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: sqrt/exp/log/pow/sin/cos/tan de MiniFloat16 dentro de tolerancia.\n");
	return 0;
}
