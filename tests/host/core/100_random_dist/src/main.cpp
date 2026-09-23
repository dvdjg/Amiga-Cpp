// ============================================================================
// Test HOST-100: distribuciones sobre el PRNG (eng::core/random).
// ============================================================================
//
// Respalda las utilidades de `eng/core/math/random.hpp`: next_range, next_symmetric,
// chance, pick, shuffle (Fisher-Yates) y next_gaussian_approx.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/100_random_dist

#include <cstdio>

#include <eng/core/math/random.hpp>

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
	std::printf("== HOST-100 random distributions ==\n");

	// --- next_range ----------------------------------------------------------
	{
		eng::Xoroshiro64pp rng {0x1234u, 0x5678u};
		bool in_range = true;
		eng::u32 distinct = 0u;
		bool seen[6] = {};
		for (int i = 0; i < 2000; ++i) {
			const eng::u32 v = eng::next_range(rng, 10u, 16u);
			in_range = in_range && v >= 10u && v < 16u;
			if (!seen[v - 10u]) {
				seen[v - 10u] = true;
				++distinct;
			}
		}
		check(in_range, "next_range dentro de [lo,hi)");
		check(distinct == 6u, "next_range cubre todo el rango");
		check(eng::next_range(rng, 5u, 5u) == 5u, "next_range rango vacío = lo");
	}

	// --- next_symmetric ------------------------------------------------------
	{
		eng::Xoroshiro64pp rng {1u, 2u};
		bool ok = true;
		for (int i = 0; i < 2000; ++i) {
			const eng::s32 v = eng::next_symmetric(rng, 7u);
			ok = ok && v >= -7 && v <= 7;
		}
		check(ok, "next_symmetric en [-range, range]");
		check(eng::next_symmetric(rng, 0u) == 0, "next_symmetric range 0");
	}

	// --- chance --------------------------------------------------------------
	{
		eng::Xoroshiro64pp rng {3u, 4u};
		check(!eng::chance(rng, 0u, 1u), "chance(0/1) = false");
		check(eng::chance(rng, 1u, 1u), "chance(1/1) = true");
		int hits = 0;
		for (int i = 0; i < 4000; ++i) {
			if (eng::chance(rng, 1u, 2u)) {
				++hits;
			}
		}
		check(hits > 1700 && hits < 2300, "chance(1/2) ~ 50%");
	}

	// --- pick ----------------------------------------------------------------
	{
		const int data[4] = {10, 20, 30, 40};
		eng::Xoroshiro64pp rng {5u, 6u};
		bool ok = true;
		for (int i = 0; i < 500; ++i) {
			const int v = eng::pick(rng, eng::Span<const int> {data, 4});
			ok = ok && (v == 10 || v == 20 || v == 30 || v == 40);
		}
		check(ok, "pick devuelve un elemento");
	}

	// --- shuffle (permutación) ----------------------------------------------
	{
		int data[10];
		for (int i = 0; i < 10; ++i) {
			data[i] = i;
		}
		eng::Xoroshiro64pp rng {7u, 8u};
		eng::shuffle(rng, eng::Span<int> {data, 10});
		bool seen[10] = {};
		bool is_perm = true;
		for (int i = 0; i < 10; ++i) {
			if (data[i] < 0 || data[i] >= 10 || seen[data[i]]) {
				is_perm = false;
				break;
			}
			seen[data[i]] = true;
		}
		check(is_perm, "shuffle es una permutación");
	}

	// --- gaussian approx -----------------------------------------------------
	{
		eng::Xoroshiro64pp rng {9u, 10u};
		bool ok = true;
		for (int i = 0; i < 2000; ++i) {
			const eng::s32 v = eng::next_gaussian_approx(rng, 100, 5u);
			ok = ok && v >= 100 - 4 * 5 && v <= 100 + 4 * 5;
		}
		check(ok, "gaussian approx dentro de ±4σ");
		check(eng::next_gaussian_approx(rng, 42, 0u) == 42, "gaussian stddev 0 = media");
	}

	// --- determinismo --------------------------------------------------------
	{
		eng::Xoroshiro64pp a {123u, 456u};
		eng::Xoroshiro64pp b {123u, 456u};
		bool same = true;
		for (int i = 0; i < 100; ++i) {
			same = same && eng::next_range(a, 0u, 1000u) == eng::next_range(b, 0u, 1000u);
		}
		check(same, "misma semilla = misma secuencia");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: distribuciones validadas.\n");
	return 0;
}
