// ============================================================================
// Test HOST-058: puente MiniFloat16 <-> coma fija retro (`eng/retro/minifloat_fixed.hpp`).
// ============================================================================
//
// Respalda:
//   - conversiones explícitas MiniFloat16 <-> `Fixed<s16,Frac>` y `fix`(4.12)/`fix88`(8.8)
//     (con saturación);
//   - producto mixto ratio·fijo (`mul_fixed`/`mul_fix`/`mul_fix88`);
//   - transformación de coordenadas fijas con matriz MF (`transform`/`transform_fix`/
//     `transform_fix88`), afín incluida.
//
// La referencia es `float` sobre las MISMAS entradas redondeadas. Como las coordenadas
// tienen 12 (o 8) bits de fracción y la matriz ~10 bits, el error útil es pequeño.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/058_minifloat_fixed

#include <cmath>
#include <cstdio>

#include <eng/retro/minifloat_fixed.hpp>

using eng::math::MiniFloat16;
using eng::retro::fix;
using eng::retro::fix88;
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

float rel_err(float got, float want) {
	const float d = std::fabs(want);
	return d < 1.0e-12f ? std::fabs(got - want) : std::fabs(got - want) / d;
}

float fx2f(fix v) { return static_cast<float>(v) / 4096.0f; }
float f88_2f(fix88 v) { return static_cast<float>(v) / 256.0f; }
fix f2fx(float v) { return static_cast<fix>(std::lround(v * 4096.0f)); }
fix88 f2f88(float v) { return static_cast<fix88>(std::lround(v * 256.0f)); }

// ---------------------------------------------------------------------------
//  1. Conversiones
// ---------------------------------------------------------------------------

void test_conversions() {
	// fix (4.12) -> MF sobre todo el rango
	{
		float mx = 0;
		bool round_ok = true;
		for (int v = -32768; v <= 32767; v += 3) {
			const float want = fx2f(static_cast<fix>(v));
			const MiniFloat16 m = er::fix_to_mf(static_cast<fix>(v));
			if (std::fabs(want) > 1.0e-4f) mx = std::fmax(mx, rel_err(static_cast<float>(m), want));
			// ida y vuelta: el MF tiene 10 bits de mantisa, así que el error absoluto
			// crece con |v| (relativo ~2^-10), no es "1 ulp de fixed".
			const int d = std::abs(static_cast<int>(er::mf_to_fix(m)) - v);
			if (std::abs(v) >= 64) {
				if (d > (std::abs(v) >> 9) + 2) round_ok = false;
			} else if (d > 2) {
				round_ok = false;
			}
		}
		std::printf("  fix_to_mf rel max %.2e\n", mx);
		check(mx <= 1.0e-3f, "fix_to_mf: rel <= 1e-3");
		check(round_ok, "fix -> MF -> fix dentro del error de la mantisa (2^-10 rel)");
	}

	// fix88 (8.8) -> MF
	{
		float mx = 0;
		for (int v = -32768; v <= 32767; v += 5) {
			const float want = f88_2f(static_cast<fix88>(v));
			const float got = static_cast<float>(er::fix88_to_mf(static_cast<fix88>(v)));
			if (std::fabs(want) > 1.0e-3f) mx = std::fmax(mx, rel_err(got, want));
		}
		std::printf("  fix88_to_mf rel max %.2e\n", mx);
		check(mx <= 1.0e-3f, "fix88_to_mf: rel <= 1e-3");
	}

	// valores exactos y saturación
	check(er::mf_to_fix(MiniFloat16(1.5f)) == 6144, "mf_to_fix(1.5) = 6144");
	check(er::mf_to_fix(MiniFloat16(-0.5f)) == -2048, "mf_to_fix(-0.5) = -2048");
	check(er::mf_to_fix(MiniFloat16(100.0f)) == 32767, "mf_to_fix(100) satura a 32767");
	check(er::mf_to_fix(MiniFloat16(-100.0f)) == -32768, "mf_to_fix(-100) satura a -32768");
	check(er::mf_to_fix88(MiniFloat16(1.5f)) == 384, "mf_to_fix88(1.5) = 384");
	check(er::mf_to_fix88(MiniFloat16(200.0f)) == 32767, "mf_to_fix88(200) satura");
	check(er::fix_to_mf(fix {4096}).raw == MiniFloat16(1.0f).raw, "fix_to_mf(4096) = 1.0");
	check(er::mf_to_fixed<12>(MiniFloat16(1.5f)).v == 6144, "API tipada coincide con la cruda");
}

// ---------------------------------------------------------------------------
//  2. Producto mixto
// ---------------------------------------------------------------------------

void test_mul() {
	// r·v con r en MF y v en fix, evitando resultados fuera del rango de fix (saturación)
	{
		float mx = 0;
		for (int a = -20; a <= 20; ++a)
			for (int b = -20; b <= 20; ++b) {
				const float r = a * 0.2f;
				const fix v = f2fx(b * 0.1f);
				const float want = r * fx2f(v);
				if (std::fabs(want) < 1.0e-3f || std::fabs(want) >= 7.9f) continue;
				mx = std::fmax(mx, rel_err(fx2f(er::mul_fix(MiniFloat16(r), v)), want));
			}
		std::printf("  mul_fix rel max %.2e\n", mx);
		check(mx <= 3.0e-3f, "mul_fix: rel <= 3e-3");
	}
	check(er::mul_fix(MiniFloat16(0.5f), f2fx(3.0f)) == 6144, "mul_fix(0.5, 3.0) = 1.5");

	// r·v con v en fix88
	{
		float mx = 0;
		for (int a = -20; a <= 20; ++a)
			for (int b = -20; b <= 20; ++b) {
				const float r = a * 0.2f;
				const fix88 v = f2f88(b * 4.0f);
				const float want = r * f88_2f(v);
				if (std::fabs(want) < 1.0e-2f || std::fabs(want) >= 127.0f) continue;
				mx = std::fmax(mx, rel_err(f88_2f(er::mul_fix88(MiniFloat16(r), v)), want));
			}
		std::printf("  mul_fix88 rel max %.2e\n", mx);
		check(mx <= 6.0e-3f, "mul_fix88: rel <= 6e-3 (8 bits de fracción)");
	}
}

// ---------------------------------------------------------------------------
//  3. Transformación de coordenadas
// ---------------------------------------------------------------------------

void test_transform() {
	// identidad: deja el punto intacto
	{
		const em::Vec<3, fix> p = {f2fx(3.4f), f2fx(-2.2f), f2fx(0.5f)};
		const auto r = er::transform_fix(em::Mat<3, MiniFloat16>::identity(), p);
		check(r.v[0] == p.v[0] && r.v[1] == p.v[1] && r.v[2] == p.v[2], "identidad deja el punto");
	}

	// rotación 90°: (3,0) -> (0,3)
	{
		const em::Mat<2, MiniFloat16> m = {
			{{MiniFloat16(0.0f), MiniFloat16(-1.0f)}, {MiniFloat16(1.0f), MiniFloat16(0.0f)}}};
		const em::Vec<2, fix> p = {f2fx(3.0f), f2fx(0.0f)};
		const auto r = er::transform_fix(m, p);
		check(r.v[0] == 0 && r.v[1] == f2fx(3.0f), "rot 90: (3,0) -> (0,3)");
	}

	// 3x3 general + traslación contra float
	{
		const float ang = 0.7f;
		const float c = std::cos(ang), s = std::sin(ang);
		const em::Mat<3, MiniFloat16> m = {
			{{MiniFloat16(c), MiniFloat16(-s), MiniFloat16(0.2f)},
			 {MiniFloat16(s), MiniFloat16(c), MiniFloat16(-0.1f)},
			 {MiniFloat16(0.0f), MiniFloat16(0.0f), MiniFloat16(1.3f)}}};
		const em::Vec<3, fix> p = {f2fx(3.4f), f2fx(-2.2f), f2fx(0.5f)};
		const em::Vec<3, fix> t = {f2fx(1.1f), f2fx(0.7f), f2fx(-0.3f)};
		const auto r = er::transform_fix(m, p);
		const auto rt = er::transform_fix(m, p, t);
		float mxf = 0, mxt = 0;
		for (int i = 0; i < 3; ++i) {
			float want = 0;
			for (int k = 0; k < 3; ++k)
				want += static_cast<float>(m.m[i][k]) * fx2f(p.v[k]);
			mxf = std::fmax(mxf, std::fabs(fx2f(r.v[i]) - want));
			mxt = std::fmax(mxt, std::fabs(fx2f(rt.v[i]) - (want + fx2f(t.v[i]))));
		}
		std::printf("  transform 3x3 abs max %.2e ; con traslacion %.2e\n", mxf, mxt);
		check(mxf <= 2.0e-3f, "transform 3x3: abs <= 2e-3");
		check(mxt <= 2.0e-3f, "transform 3x3 + t: abs <= 2e-3");

		// la vía tipada y la cruda coinciden
		em::Vec<3, er::Coord12> q {};
		for (int i = 0; i < 3; ++i) q.v[i].v = p.v[i];
		const auto qt = er::transform(m, q);
		bool same = true;
		for (int i = 0; i < 3; ++i) same = same && (qt.v[i].v == r.v[i]);
		check(same, "transform tipada == transform_fix cruda");
	}

	// fix88 contra float
	{
		const em::Mat<2, MiniFloat16> m = {
			{{MiniFloat16(0.75f), MiniFloat16(-0.5f)}, {MiniFloat16(0.5f), MiniFloat16(0.75f)}}};
		const em::Vec<2, fix88> p = {f2f88(20.0f), f2f88(-10.0f)};
		const auto r = er::transform_fix88(m, p);
		float mx = 0;
		for (int i = 0; i < 2; ++i) {
			float want = 0;
			for (int k = 0; k < 2; ++k)
				want += static_cast<float>(m.m[i][k]) * f88_2f(p.v[k]);
			mx = std::fmax(mx, std::fabs(f88_2f(r.v[i]) - want));
		}
		std::printf("  transform 2x2 (fix88) abs max %.2e\n", mx);
		check(mx <= 0.05f, "transform fix88: abs <= 5e-2");
	}

	// saturación del resultado
	{
		const em::Mat<2, MiniFloat16> big = {
			{{MiniFloat16(4.0f), MiniFloat16(0.0f)}, {MiniFloat16(0.0f), MiniFloat16(4.0f)}}};
		const em::Vec<2, fix> p = {f2fx(7.0f), f2fx(-7.0f)};
		const auto r = er::transform_fix(big, p);
		check(r.v[0] == 32767 && r.v[1] == -32768, "transform satura fuera de rango");
	}
}

// ---------------------------------------------------------------------------
//  4. 4x4 homogéneo (proyección) y atajo 2D de `lib2d`
// ---------------------------------------------------------------------------

void test_homogeneous_and_2d() {
	// 4x4 afín: M·(p,1) deja w = 1 y transforma la posición
	{
		const em::Mat<4, MiniFloat16> m = {
			{{MiniFloat16(1.5f), MiniFloat16(0.0f), MiniFloat16(0.0f), MiniFloat16(2.0f)},
			 {MiniFloat16(0.0f), MiniFloat16(0.5f), MiniFloat16(0.0f), MiniFloat16(-1.0f)},
			 {MiniFloat16(0.0f), MiniFloat16(0.0f), MiniFloat16(2.0f), MiniFloat16(0.5f)},
			 {MiniFloat16(0.0f), MiniFloat16(0.0f), MiniFloat16(0.0f), MiniFloat16(1.0f)}}};
		const em::Vec<3, er::Coord12> p = {f2fx(2.0f), f2fx(1.0f), f2fx(3.0f)};
		const auto h = er::transform_point(m, p);
		float mx = 0;
		const float wf[3] = {1.5f * 2.0f + 2.0f, 0.5f * 1.0f - 1.0f, 2.0f * 3.0f + 0.5f};
		for (int i = 0; i < 3; ++i) mx = std::fmax(mx, std::fabs(fx2f(h.v[i].v) - wf[i]));
		std::printf("  4x4 afín abs max %.2e ; w = %.4f\n", mx, fx2f(h.v[3].v));
		check(mx <= 2.0e-3f, "4x4 afín: posicion correcta");
		check(h.v[3].v == f2fx(1.0f), "4x4 afín: w se mantiene 1.0");
	}

	// 4x4 de proyección: x' = f·x, y' = f·y, w' = z -> pantalla (f·x/z, f·y/z)
	{
		const em::Mat<4, MiniFloat16> m = {
			{{MiniFloat16(1.0f), MiniFloat16(0.0f), MiniFloat16(0.0f), MiniFloat16(0.0f)},
			 {MiniFloat16(0.0f), MiniFloat16(1.0f), MiniFloat16(0.0f), MiniFloat16(0.0f)},
			 {MiniFloat16(0.0f), MiniFloat16(0.0f), MiniFloat16(1.0f), MiniFloat16(0.0f)},
			 {MiniFloat16(0.0f), MiniFloat16(0.0f), MiniFloat16(1.0f), MiniFloat16(0.0f)}}};
		const em::Vec<3, er::Coord12> p = {f2fx(2.0f), f2fx(1.0f), f2fx(4.0f)};
		const auto s = er::project(m, p);
		const float want[2] = {2.0f / 4.0f, 1.0f / 4.0f};
		const float got[2] = {static_cast<float>(s.v[0]), static_cast<float>(s.v[1])};
		const float mx = std::fmax(std::fabs(got[0] - want[0]), std::fabs(got[1] - want[1]));
		std::printf("  project: (%.4f, %.4f) want (%.4f, %.4f) abs max %.2e\n", got[0], got[1],
			    want[0], want[1], mx);
		check(mx <= 3.0e-3f, "project: division por w correcta");
	}

	// Atajo 2D de `lib2d`: Vec2 = Vec<2,q0> (pixeles enteros) por una matriz 2x2 de MF
	{
		const float ang = 0.7f;
		const em::Mat<2, MiniFloat16> m = {
			{{MiniFloat16(std::cos(ang)), MiniFloat16(-std::sin(ang))},
			 {MiniFloat16(std::sin(ang)), MiniFloat16(std::cos(ang))}}};
		const em::Vec<2, er::q0> v2 = {er::q0 {10}, er::q0 {0}};
		const auto r = er::transform(m, v2); // mismo template que 4.12/8.8
		const float wx = std::cos(ang) * 10.0f, wy = std::sin(ang) * 10.0f;
		const float mx = std::fmax(std::fabs(static_cast<float>(r.v[0].v) - wx),
					   std::fabs(static_cast<float>(r.v[1].v) - wy));
		std::printf("  transform 2D (Vec2/q0): (%d,%d) abs max %.2e\n",
			    static_cast<int>(r.v[0].v), static_cast<int>(r.v[1].v), mx);
		check(mx <= 1.0f, "transform sobre Vec2 (q0): error <= 1 pixel");
	}
}

} // namespace

int main() {
	std::printf("== HOST-058 MiniFloat16 <-> fixed ==\n");
	test_conversions();
	test_mul();
	test_transform();
	test_homogeneous_and_2d();
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: puente MiniFloat16<->fix/fix88 (conversion, mul, transform) validado.\n");
	return 0;
}
