// ============================================================================
// Test HOST-060: ruido procedural genérico (`eng/core/math/noise.hpp`).
// ============================================================================
//
// El MISMO algoritmo (value noise + fbm) se ejecuta con `double` (referencia) y con
// `MiniFloat16`, sobre las mismas entradas redondeadas. Comprueba determinismo, rango
// [0,1], ruido periódico (tileable) y fbm1/fbm2/fbm3.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/060_noise

#include <cmath>
#include <cstdio>

// `noise.hpp` es genérico y NO incluye escalares concretos (§1.10): el test trae el suyo.
#include <eng/core/math/minifloat.hpp>
#include <eng/core/math/noise.hpp>

using eng::math::MiniFloat16;
namespace em = eng::math;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_determinism_and_range() {
	// determinista: dos llamadas idénticas dan el mismo raw
	const MiniFloat16 a = em::value_noise2(MiniFloat16(3.7f), MiniFloat16(-1.3f), 42u);
	const MiniFloat16 b = em::value_noise2(MiniFloat16(3.7f), MiniFloat16(-1.3f), 42u);
	check(a.raw == b.raw, "value_noise2 determinista");
	check(em::value_noise2(MiniFloat16(3.7f), MiniFloat16(-1.3f), 43u).raw != a.raw,
	      "seeds distintos dan valores distintos");

	// rango [0,1]
	bool in_range = true;
	for (int i = 0; i < 500; ++i) {
		const float x = -20.0f + i * 0.08f;
		const float y = 7.0f - i * 0.03f;
		const float v = static_cast<float>(em::value_noise2(MiniFloat16(x), MiniFloat16(y), 1u));
		const float f = static_cast<float>(em::fbm2(MiniFloat16(x * 0.1f), MiniFloat16(y * 0.1f), 7u,
							    4, MiniFloat16(2.0f), MiniFloat16(0.5f)));
		if (v < 0.0f || v > 1.0f || f < 0.0f || f > 1.0f) in_range = false;
	}
	check(in_range, "value_noise2/fbm2 en [0,1]");
}

void test_vs_double() {
	// value noise 1D/2D/3D y fbm: MF vs double sobre la misma rejilla
	float e1 = 0, e2 = 0, e3 = 0, ef = 0;
	for (int i = 0; i < 300; ++i) {
		const float x = -8.0f + i * 0.05f;
		const float y = 2.0f + i * 0.031f;
		const float z = -3.0f + i * 0.017f;

		const float r1m = static_cast<float>(em::value_noise1(MiniFloat16(x), 5u));
		const float r1d = static_cast<float>(em::value_noise1<double>(static_cast<double>(x), 5u));
		e1 = std::fmax(e1, std::fabs(r1m - r1d));

		const float r2m =
			static_cast<float>(em::value_noise2(MiniFloat16(x), MiniFloat16(y), 9u));
		const float r2d = static_cast<float>(em::value_noise2<double>(static_cast<double>(x),
									     static_cast<double>(y), 9u));
		e2 = std::fmax(e2, std::fabs(r2m - r2d));

		const float r3m = static_cast<float>(
			em::value_noise3(MiniFloat16(x), MiniFloat16(y), MiniFloat16(z), 3u));
		const float r3d = static_cast<float>(em::value_noise3<double>(
			static_cast<double>(x), static_cast<double>(y), static_cast<double>(z), 3u));
		e3 = std::fmax(e3, std::fabs(r3m - r3d));

		const float fbm = static_cast<float>(em::fbm2(MiniFloat16(x), MiniFloat16(y), 11u, 5,
							      MiniFloat16(2.0f), MiniFloat16(0.5f)));
		const float fbd = static_cast<float>(em::fbm2<double>(static_cast<double>(x),
								     static_cast<double>(y), 11u, 5, 2.0, 0.5));
		ef = std::fmax(ef, std::fabs(fbm - fbd));
	}
	std::printf("  MF vs double: 1D %.2e  2D %.2e  3D %.2e  fbm %.2e\n", e1, e2, e3, ef);
	check(e1 <= 1.5e-2f, "value_noise1 MF ~ double");
	check(e2 <= 2.0e-2f, "value_noise2 MF ~ double");
	check(e3 <= 2.5e-2f, "value_noise3 MF ~ double");
	check(ef <= 2.0e-2f, "fbm2 MF ~ double");
}

void test_fbm_shape() {
	// fbm con más octavas añade detalle pero se mantiene en [0,1] y finito
	bool ok = true;
	for (int i = 0; i < 200; ++i) {
		const float x = i * 0.13f, y = i * -0.07f;
		const MiniFloat16 v = em::fbm2(MiniFloat16(x), MiniFloat16(y), 21u, 7,
					       MiniFloat16(2.0f), MiniFloat16(0.5f));
		const float f = static_cast<float>(v);
		if (!(f >= 0.0f && f <= 1.0f)) ok = false;
	}
	check(ok, "fbm2 de 7 octavas finito y en [0,1]");
}

void test_periodic_and_fbm13() {
	// fbm1 / fbm3 frente a double
	{
		float e1 = 0, e3 = 0;
		for (int i = 0; i < 200; ++i) {
			const float x = -6.0f + i * 0.06f;
			const float y = 1.0f - i * 0.02f;
			const float z = 0.5f + i * 0.011f;
			e1 = std::fmax(e1, std::fabs(static_cast<float>(em::fbm1(
					MiniFloat16(x), 5u, 4, MiniFloat16(2.0f), MiniFloat16(0.5f))) -
				static_cast<float>(em::fbm1<double>(static_cast<double>(x), 5u, 4, 2.0, 0.5))));
			e3 = std::fmax(e3, std::fabs(static_cast<float>(em::fbm3(
					MiniFloat16(x), MiniFloat16(y), MiniFloat16(z), 3u, 3,
					MiniFloat16(2.0f), MiniFloat16(0.5f))) -
				static_cast<float>(em::fbm3<double>(static_cast<double>(x),
								    static_cast<double>(y),
								    static_cast<double>(z), 3u, 3, 2.0, 0.5))));
		}
		std::printf("  fbm1 MF~double %.2e   fbm3 MF~double %.2e\n", e1, e3);
		check(e1 <= 2.0e-2f, "fbm1 MF ~ double");
		check(e3 <= 2.5e-2f, "fbm3 MF ~ double");
	}

	// periodicidad (tileable): noise(x+P) == noise(x), en double y en MF
	{
		const int P = 8;
		float md = 0, mm = 0, fd = 0;
		for (int i = 0; i < 64; ++i) {
			const float x = i * 0.12f, y = 1.0f - i * 0.05f;
			md = std::fmax(
				md, std::fabs(static_cast<float>(em::value_noise2<double>(
						      static_cast<double>(x) + P, static_cast<double>(y), 4u, P)) -
					      static_cast<float>(em::value_noise2<double>(
						      static_cast<double>(x), static_cast<double>(y), 4u, P))));
			mm = std::fmax(
				mm, std::fabs(static_cast<float>(em::value_noise2(
						      MiniFloat16(x + P), MiniFloat16(y), 4u, P)) -
					      static_cast<float>(em::value_noise2(
						      MiniFloat16(x), MiniFloat16(y), 4u, P))));
			fd = std::fmax(
				fd, std::fabs(static_cast<float>(em::fbm2<double>(
						      static_cast<double>(x) + P, static_cast<double>(y), 6u,
						      4, 2.0, 0.5, P)) -
					      static_cast<float>(em::fbm2<double>(
						      static_cast<double>(x), static_cast<double>(y), 6u,
						      4, 2.0, 0.5, P))));
		}
		std::printf("  periodicidad: vn2 double %.2e  vn2 MF %.2e  fbm2 double %.2e\n", md, mm, fd);
		check(md <= 1.0e-9f, "value_noise2 periodico (double) sin costura");
		check(mm <= 1.5e-2f, "value_noise2 periodico (MF) sin costura");
		check(fd <= 1.0e-9f, "fbm2 periodico (double, lac=2) sin costura");
	}
}

} // namespace

int main() {
	std::printf("== HOST-060 noise (value noise + fbm) ==\n");
	test_determinism_and_range();
	test_vs_double();
	test_fbm_shape();
	test_periodic_and_fbm13();
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: value noise/fbm genericos (double y MiniFloat16) validados.\n");
	return 0;
}
