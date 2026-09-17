// ============================================================================
// Test HOST-101: worley/cellular y variantes de fbm (eng::math/noise).
// ============================================================================
//
// Respalda `eng/core/noise.hpp`: worley2_sq/worley2, turbulence2 y ridged2.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/101_noise_extra

#include <cmath>
#include <cstdio>

#include <eng/core/minifloat_math.hpp>
#include <eng/core/noise.hpp>

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

} // namespace

int main() {
	std::printf("== HOST-101 noise worley/turbulence ==\n");

	const eng::u32 seed = 1234u;

	// --- worley (doble) ------------------------------------------------------
	{
		const double sq = em::worley2_sq(1.5, 2.5, seed);
		check(sq >= 0.0 && sq < 4.0, "worley2_sq en [0,4)");
		const double d = em::worley2(1.5, 2.5, seed);
		check(std::fabs(d - std::sqrt(sq)) <= 1.0e-12, "worley2 = sqrt(worley2_sq)");
		check(em::worley2_sq(1.5, 2.5, seed) == sq, "worley determinista");

		// Periodicidad: x y x+period dan lo mismo.
		const double a = em::worley2_sq(1.3, 2.7, seed, 8);
		const double b = em::worley2_sq(9.3, 2.7, seed, 8);
		check(std::fabs(a - b) <= 1.0e-12, "worley periódico con period");
	}

	// --- turbulence / ridged -------------------------------------------------
	{
		const double t = em::turbulence2(1.5, 2.5, seed, 4, 2.0, 0.5);
		check(t >= 0.0 && t <= 1.0, "turbulence2 en [0,1]");
		const double r = em::ridged2(1.5, 2.5, seed, 4, 2.0, 0.5);
		check(r >= 0.0 && r <= 1.0, "ridged2 en [0,1]");
		check(em::turbulence2(1.5, 2.5, seed, 4, 2.0, 0.5) == t, "turbulence determinista");
	}

	// --- MiniFloat16 ---------------------------------------------------------
	{
		const MiniFloat16 x = MiniFloat16(1.5f);
		const MiniFloat16 y = MiniFloat16(2.5f);
		const double sq = em::to_double(em::worley2_sq(x, y, seed));
		check(sq >= 0.0 && sq < 4.0, "MF worley2_sq en [0,4)");
		const double d = em::to_double(em::worley2(x, y, seed));
		check(d >= 0.0, "MF worley2 >= 0");
		const double t = em::to_double(em::turbulence2(x, y, seed, 3, MiniFloat16(2.0f),
								MiniFloat16(0.5f)));
		check(t >= -1.0e-3 && t <= 1.0 + 1.0e-3, "MF turbulence2 ~ [0,1]");
		std::printf("  MF worley2=%.4f turbulence=%.4f\n", d, t);
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: noise extra validado.\n");
	return 0;
}
