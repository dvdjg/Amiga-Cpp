// ============================================================================
// Test HOST-059: base matemática genérica (escalar-independiente).
// ============================================================================
//
// Respalda `eng/core/numeric_traits.hpp` (rasgos y guards de compilación),
// `eng/core/interp.hpp` (interpolación/easing) y `eng/core/geometry.hpp` (vocabulario
// geométrico). Los MISMOS algoritmos se ejercitan con `double` y con `MiniFloat16`.
//
// La referencia se calcula en `double` a partir de las MISMAS entradas que recibe el
// algoritmo (`to_double` del valor redondeado), no de los literales originales.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/059_scalar_math

#include <cmath>
#include <cstdio>

#include <eng/core/geometry.hpp>
#include <eng/core/interp.hpp>
#include <eng/core/minifloat_math.hpp>
#include <eng/core/numeric_traits.hpp>
#include <eng/retro/fixed_q.hpp>

using eng::math::MiniFloat16;
namespace em = eng::math;
namespace er = eng::retro;

// ---------------------------------------------------------------------------
//  Guards de compilación (lo que se pidió: que el tipo avise ANTES de correr)
// ---------------------------------------------------------------------------

// El dominio fiable de la trigonometría (±2π) cabe en MiniFloat16…
static_assert(em::range_fits<MiniFloat16, -6.29, 6.29>(), "MF cubre el dominio de sin/cos");
// …pero un 4.12 (rango ±7.99) NO cubre ±10: require_range<q12,-10,10>() daría error.
static_assert(em::range_fits<er::q12, -7.9, 7.9>(), "4.12 cubre +-7.9");
static_assert(!em::range_fits<er::q12, -10.0, 10.0>(), "4.12 no cubre +-10");
// La división la declara cada escalar (el núcleo fixed NO la tiene a propósito).
static_assert(!em::numeric_traits<er::q12>::has_division, "Fixed no divide");
static_assert(em::numeric_traits<MiniFloat16>::has_division, "MF divide");
static_assert(em::numeric_traits<MiniFloat16>::max_finite == 65504.0, "MF max finito");
static_assert(em::numeric_traits<MiniFloat16>::has_nan == false, "MF no tiene NaN");

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// ---------------------------------------------------------------------------
//  1. Interpolación frente a la referencia en double
// ---------------------------------------------------------------------------

template <typename S>
void interp_for(const char* tag, float tol_lerp, float tol_ease) {
	const S a(1.3f), b(4.7f);
	const double ad = em::to_double(a), bd = em::to_double(b);
	float e_lerp = 0, e_smooth = 0, e_smoother = 0;
	for (int i = 0; i <= 100; ++i) {
		const S t(i / 100.0f);
		const double td = em::to_double(t);
		e_lerp = std::fmax(e_lerp, std::fabs(static_cast<float>(em::lerp(a, b, t)) -
						     static_cast<float>(ad + (bd - ad) * td)));
		e_smooth = std::fmax(e_smooth, std::fabs(static_cast<float>(em::smoothstep(t)) -
							 static_cast<float>(td * td * (3.0 - 2.0 * td))));
		e_smoother = std::fmax(
			e_smoother,
			std::fabs(static_cast<float>(em::smootherstep(t)) -
				  static_cast<float>(td * td * td * (td * (td * 6.0 - 15.0) + 10.0))));
	}
	std::printf("  %s lerp %.2e  smoothstep %.2e  smootherstep %.2e\n", tag, e_lerp, e_smooth,
		    e_smoother);
	check(e_lerp <= tol_lerp, "lerp dentro de tolerancia");
	check(e_smooth <= tol_ease, "smoothstep dentro de tolerancia");
	check(e_smoother <= tol_ease, "smootherstep dentro de tolerancia");
}

void test_interp() {
	interp_for<double>("double", 1.0e-6f, 1.0e-6f);
	interp_for<MiniFloat16>("MF    ", 3.0e-3f, 1.5e-2f);

	// remap/inv_lerp (solo escalares con division)
	check(std::fabs(em::remap(2.5, 0.0, 10.0, 0.0, 1.0) - 0.25) < 1.0e-12, "remap double");
	check(std::fabs(em::inv_lerp(1.0, 5.0, 3.0) - 0.5) < 1.0e-12, "inv_lerp double");
	check(std::fabs(static_cast<float>(em::remap(MiniFloat16(2.5f), MiniFloat16(0.0f),
						     MiniFloat16(10.0f), MiniFloat16(0.0f),
						     MiniFloat16(1.0f))) -
			0.25f) < 3.0e-3f,
	      "remap MF");
	check(em::saturate(1.7) == 1.0 && em::saturate(-0.3) == 0.0, "saturate double");
	check(em::saturate(MiniFloat16(1.7f)).raw == MiniFloat16(1.0f).raw, "saturate MF");
}

// ---------------------------------------------------------------------------
//  2. Geometría frente a la referencia en double
// ---------------------------------------------------------------------------

template <typename S>
void geometry_for(const char* tag, float tol) {
	(void)tag;
	using V2 = em::Vec<2, S>;
	using V3 = em::Vec<3, S>;

	// dot / cross2 / perp
	const V2 a = {S(1.3f), S(-0.7f)};
	const V2 b = {S(0.4f), S(1.1f)};
	const double a0 = em::to_double(a.v[0]), a1 = em::to_double(a.v[1]);
	const double b0 = em::to_double(b.v[0]), b1 = em::to_double(b.v[1]);
	check(std::fabs(static_cast<float>(em::dot(a, b)) - static_cast<float>(a0 * b0 + a1 * b1)) <= tol,
	      "dot");
	check(std::fabs(static_cast<float>(em::cross2(a, b)) - static_cast<float>(a0 * b1 - a1 * b0)) <= tol,
	      "cross2");
	const V2 p = em::perp(a);
	check(static_cast<float>(p.v[0]) == -static_cast<float>(a.v[1]), "perp.x");

	// rotate2 con (cos,sin) redondeados por el escalar
	{
		const S c(std::cos(0.6f)), s(std::sin(0.6f));
		const double cd = em::to_double(c), sd = em::to_double(s);
		const V2 r = em::rotate2(a, c, s);
		const float rx = static_cast<float>(cd * a0 - sd * a1);
		const float ry = static_cast<float>(sd * a0 + cd * a1);
		check(std::fabs(static_cast<float>(r.v[0]) - rx) <= tol &&
			      std::fabs(static_cast<float>(r.v[1]) - ry) <= tol,
		      "rotate2");
	}

	// rotate2 por ángulo: `scalar_sincos` en una sola pasada -> mismas componentes
	{
		S sc_sin {};
		S sc_cos {};
		em::scalar_sincos<S>::op(S(0.6f), sc_sin, sc_cos);
		const V2 r_ang = em::rotate2(a, S(0.6f));
		const V2 r_cs = em::rotate2(a, sc_cos, sc_sin);
		check(em::to_double(r_ang.v[0]) == em::to_double(r_cs.v[0]) &&
			      em::to_double(r_ang.v[1]) == em::to_double(r_cs.v[1]),
		      "rotate2(ángulo) = rotate2(cos,sin)");
	}

	// angle_of / from_angle / angle_to (apuntado): atan2 + sincos del escalar
	{
		const V2 up {S(0.0f), S(1.0f)};
		const V2 origin {S(0.0f), S(0.0f)};
		const double half_pi = 1.57079632679489661923;
		check(std::fabs(em::to_double(em::angle_of(up)) - half_pi) <=
			      static_cast<double>(tol) * 3.0,
		      "angle_of(0,1) = pi/2");
		const V2 unit = em::from_angle(S(1.5707963f));
		check(std::fabs(em::to_double(unit.v[1]) - 1.0) <= static_cast<double>(tol) * 3.0,
		      "from_angle(pi/2).y = 1");
		check(std::fabs(em::to_double(em::angle_to(origin, up)) - half_pi) <=
			      static_cast<double>(tol) * 3.0,
		      "angle_to((0,0),(0,1)) = pi/2");
	}

	// length / distance / normalize
	{
		const V3 u = {S(1.5f), S(-2.0f), S(0.75f)};
		const double ux = em::to_double(u.v[0]), uy = em::to_double(u.v[1]),
			     uz = em::to_double(u.v[2]);
		const double len_ref = std::sqrt(ux * ux + uy * uy + uz * uz);
		check(std::fabs(static_cast<float>(em::length(u)) - static_cast<float>(len_ref)) <= tol * 3.0f,
		      "length");
		check(std::fabs(static_cast<float>(em::length(em::normalize(u))) - 1.0f) <= tol * 3.0f,
		      "|normalize| = 1");
		const V3 v = {S(-0.5f), S(1.0f), S(0.25f)};
		const double vx = em::to_double(v.v[0]), vy = em::to_double(v.v[1]),
			     vz = em::to_double(v.v[2]);
		check(std::fabs(static_cast<float>(em::distance(u, v)) -
				static_cast<float>(std::sqrt((ux - vx) * (ux - vx) + (uy - vy) * (uy - vy) +
							     (uz - vz) * (uz - vz)))) <= tol * 3.0f,
		      "distance");
	}

	// project + reject = v ; reflect invierte la componente normal
	{
		const V3 v = {S(2.0f), S(1.0f), S(-0.5f)};
		const V3 axis = em::normalize(V3 {S(1.0f), S(1.0f), S(0.0f)});
		const V3 proj = em::project(v, axis);
		const V3 rej = em::reject(v, axis);
		for (int i = 0; i < 3; ++i)
			check(std::fabs(static_cast<float>(proj.v[i] + rej.v[i]) - static_cast<float>(v.v[i])) <= tol,
			      "project + reject = v");
		const V3 refl = em::reflect(v, axis);
		check(std::fabs(static_cast<float>(em::dot(refl, axis)) +
				static_cast<float>(em::dot(v, axis))) <= tol,
		      "reflect invierte la componente normal");
	}
}

void test_geometry() {
	geometry_for<double>("double", 1.0e-9f);
	geometry_for<MiniFloat16>("MF    ", 8.0e-3f);

	// vlerp / clamp de vector
	{
		using V2 = em::Vec<2, MiniFloat16>;
		const V2 a = {MiniFloat16(0.0f), MiniFloat16(1.0f)};
		const V2 b = {MiniFloat16(4.0f), MiniFloat16(-1.0f)};
		const V2 m = em::vlerp(a, b, MiniFloat16(0.5f));
		check(std::fabs(static_cast<float>(m.v[0]) - 2.0f) < 3.0e-3f &&
			      std::fabs(static_cast<float>(m.v[1])) < 3.0e-3f,
		      "vlerp");
		const V2 c = em::clamp(V2 {MiniFloat16(9.0f), MiniFloat16(-9.0f)}, MiniFloat16(0.0f),
				       MiniFloat16(5.0f));
		check(c.v[0].raw == MiniFloat16(5.0f).raw && c.v[1].raw == MiniFloat16(0.0f).raw,
		      "clamp de vector");
	}
}

void test_fixed() {
	// Los MISMOS algoritmos sobre un fixed 4.12 (q12), donde el producto cambia de
	// exponente: `mul_norm` los hace genéricos. Sin división (lo que la divide no aplica).
	using Q = er::q12;
	const float tol = 6.0e-3f;

	// lerp / smoothstep (smootherstep NO: su coeficiente 15 no cabe en q12; el
	// require_range lo avisa al compilar)
	{
		float e_lerp = 0, e_sm = 0;
		for (int i = 0; i <= 32; ++i) {
			const Q t {static_cast<eng::s16>(i * 128)}; // 0..4096 (0..1)
			const Q a {4096}, b {8192};
			const double td = em::to_double(t);
			e_lerp = std::fmax(e_lerp, std::fabs(em::to_double(em::lerp(a, b, t)) -
							     (1.0 + (2.0 - 1.0) * td)));
			e_sm = std::fmax(e_sm, std::fabs(em::to_double(em::smoothstep(t)) -
							 td * td * (3.0 - 2.0 * td)));
		}
		std::printf("  q12 lerp %.2e  smoothstep %.2e\n", e_lerp, e_sm);
		check(e_lerp <= tol, "q12 lerp");
		check(e_sm <= tol, "q12 smoothstep");
	}

	// geometría (dot/cross2/perp/rotate2/vscale/vlerp), sin sqrt ni división
	{
		using V2 = em::Vec<2, Q>;
		const V2 a = {Q {4096}, Q {2048}}; // (1, 0.5)
		const V2 b = {Q {2048}, Q {4096}}; // (0.5, 1)
		const double a0 = em::to_double(a.v[0]), a1 = em::to_double(a.v[1]);
		const double b0 = em::to_double(b.v[0]), b1 = em::to_double(b.v[1]);
		check(std::fabs(em::to_double(em::dot(a, b)) - (a0 * b0 + a1 * b1)) <= tol, "q12 dot");
		check(std::fabs(em::to_double(em::cross2(a, b)) - (a0 * b1 - a1 * b0)) <= tol,
		      "q12 cross2");
		const V2 p = em::perp(a);
		check(p.v[0].v == static_cast<eng::s16>(-a.v[1].v) && p.v[1].v == a.v[0].v, "q12 perp");

		const Q c {3670}, s {1820}; // ~cos(0.46), ~sin(0.46) en 4.12
		const double cd = em::to_double(c), sd = em::to_double(s);
		const V2 r = em::rotate2(a, c, s);
		check(std::fabs(em::to_double(r.v[0]) - (cd * a0 - sd * a1)) <= tol &&
			      std::fabs(em::to_double(r.v[1]) - (sd * a0 + cd * a1)) <= tol,
		      "q12 rotate2");

		const V2 sc = em::vscale(a, Q {2048}); // *0.5
		check(std::fabs(em::to_double(sc.v[0]) - a0 * 0.5) <= tol &&
			      std::fabs(em::to_double(sc.v[1]) - a1 * 0.5) <= tol,
		      "q12 vscale");
		const V2 lv = em::vlerp(a, b, Q {2048}); // mezcla al 50%
		check(std::fabs(em::to_double(lv.v[0]) - (a0 + (b0 - a0) * 0.5)) <= tol &&
			      std::fabs(em::to_double(lv.v[1]) - (a1 + (b1 - a1) * 0.5)) <= tol,
		      "q12 vlerp");
	}

	// remap/inv_lerp: división EXPLÍCITA (div_norm), sin añadir operator/
	{
		const Q a {4096}, b {8192}, v {6144}; // 1, 2, 1.5
		check(std::fabs(em::to_double(em::inv_lerp(a, b, v)) - 0.5) <= tol, "q12 inv_lerp");
		const Q r = em::remap(Q {4096}, Q {0}, Q {8192}, Q {0}, Q {4096}); // 1->[0,2]->[0,1]
		check(std::fabs(em::to_double(r) - 0.5) <= tol, "q12 remap");
	}

	// dot FUSIONADO de 3-4 pares: el acumulador satura (no envuelve) en el extremo, y el
	// estrechado final satura SIEMPRE (independiente de la política de los operandos)
	{
		using QS = er::q12_sat;
		const QS ms {32767};
		check(em::dot(ms, ms, ms, ms, ms, ms).v == 32767, "q12_sat dot(3) satura");
		check(em::dot(ms, ms, ms, ms, ms, ms, ms, ms).v == 32767, "q12_sat dot(4) satura");
		const Q m {32767};
		check(em::dot(m, m, m, m, m, m).v == 32767, "q12 (politica por defecto) dot(3) satura");
		check(em::dot(m, m, m, m, m, m, m, m).v == 32767, "q12 (politica por defecto) dot(4) satura");
	}

	// FMA sobre fixed: un solo redondeo
	{
		check(em::mul_add(Q {4096}, Q {4096}, Q {4096}).v == Q {8192}.v, "q12 mul_add(1,1,1) = 2");
		check(em::mac(Q {8192}, Q {2048}, Q {4096}).v == Q {8192}.v, "q12 mac(2,0.5,1) = 2");
		const Q a {6144}, b {2048}, c {1024}; // 1.5 * 0.5 + 0.25 = 1.0
		check(em::to_double(em::mul_add(a, b, c)) == 1.0, "q12 mul_add(1.5,0.5,0.25) = 1.0");
	}
}

} // namespace

int main() {
	std::printf("== HOST-059 scalar math (interp + geometry) ==\n");
	test_interp();
	test_geometry();
	test_fixed();
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: interp/geometry genéricos (double y MiniFloat16) y rasgos validados.\n");
	return 0;
}
