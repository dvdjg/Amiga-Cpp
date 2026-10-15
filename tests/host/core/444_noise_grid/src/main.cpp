// ============================================================================
// Test HOST-444: rejilla de nodos de ruido (`NoiseGrid2` + `*_cached`).
// ============================================================================
//
// El camino con rejilla precalculada debe dar EXACTAMENTE el mismo valor que las
// funciones libres (`fbm2`/`turbulence2`/`ridged2`): mismas operaciones, mismo orden.
// Se comprueba bit a bit con DOS escalares (`MiniFloat16` y `float`), varios seeds,
// octavas y periodos, y el camino de repliegue (periodo 0 o rejilla que no cabe).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/444_noise_grid

#include <cstdio>

// `noise.hpp` es genérico y NO incluye escalares concretos (§1.11): el test trae los suyos.
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

/// Coordenadas de prueba en pasos de 1/4 (representables en MiniFloat16 sin ruido extra).
template <class S>
S coord(int i) {
	return S(static_cast<float>(i) * 0.25f);
}

/// Compara los tres caminos cacheados con las funciones libres, bit a bit.
template <class S>
void test_equivalence(const char* tag) {
	const S two = em::scalar_traits<S>::from_int(2);
	const S half = em::div_norm(em::scalar_traits<S>::one(), two);
	const eng::u32 seeds[] = {0x1u, 0xABCDEFu};
	const int octaves_list[] = {1, 3, 4};
	const int periods[] = {1, 2, 4};
	for (eng::u32 seed : seeds) {
		for (int oct : octaves_list) {
			for (int per : periods) {
				em::NoiseGrid2<S> grid {};
				check(grid.build(seed, oct, per), tag);
				for (int y = 0; y < 24; ++y) {
					for (int x = 0; x < 24; ++x) {
						const S fx = coord<S>(x);
						const S fy = coord<S>(y);
						check(grid.fbm2(fx, fy, two, half) ==
							      em::fbm2(fx, fy, seed, oct, two, half, per),
						      tag);
						check(grid.turbulence2(fx, fy, two, half) ==
							      em::turbulence2(fx, fy, seed, oct, two, half, per),
						      tag);
						check(grid.ridged2(fx, fy, two, half) ==
							      em::ridged2(fx, fy, seed, oct, two, half, per),
						      tag);
					}
				}
			}
		}
	}
}

/// Los atajos `*_cached` (rejilla estática) coinciden con las funciones libres, incluido
/// el repliegue cuando el periodo es 0 (no tileable) o la rejilla no cabe.
template <class S>
void test_cached_shortcuts(const char* tag) {
	const S two = em::scalar_traits<S>::from_int(2);
	const S half = em::div_norm(em::scalar_traits<S>::one(), two);
	// period = 0: repliegue a la función libre (idéntico).
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			const S fx = coord<S>(x);
			const S fy = coord<S>(y);
			check(em::fbm2_cached(fx, fy, 7u, 3, two, half, 0) ==
				      em::fbm2(fx, fy, 7u, 3, two, half, 0),
			      tag);
		}
	}
	// Rejilla que no cabe (period 32 x 6 octavas): repliegue idéntico.
	{
		em::NoiseGrid2<S> big {};
		check(!big.build(1u, 6, 32), tag);
	}
	// Cambio de parámetros: reconstruye y sigue coincidiendo.
	check(em::fbm2_cached(coord<S>(3), coord<S>(5), 1u, 3, two, half, 2) ==
		      em::fbm2(coord<S>(3), coord<S>(5), 1u, 3, two, half, 2),
	      tag);
	check(em::fbm2_cached(coord<S>(3), coord<S>(5), 999u, 4, two, half, 4) ==
		      em::fbm2(coord<S>(3), coord<S>(5), 999u, 4, two, half, 4),
	      tag);
	check(em::fbm2_cached(coord<S>(3), coord<S>(5), 1u, 3, two, half, 2) ==
		      em::fbm2(coord<S>(3), coord<S>(5), 1u, 3, two, half, 2),
	      tag);
	check(em::turbulence2_cached(coord<S>(6), coord<S>(2), 5u, 3, two, half, 2) ==
		      em::turbulence2(coord<S>(6), coord<S>(2), 5u, 3, two, half, 2),
	      tag);
	check(em::ridged2_cached(coord<S>(6), coord<S>(2), 5u, 3, two, half, 2) ==
		      em::ridged2(coord<S>(6), coord<S>(2), 5u, 3, two, half, 2),
	      tag);
	// Periodicidad de la rejilla: valor en x y x+p coinciden (tileable).
	const S p = em::scalar_traits<S>::from_int(2);
	check(em::fbm2_cached(coord<S>(1), coord<S>(3), 42u, 3, two, half, 2) ==
		      em::fbm2_cached(coord<S>(1) + p, coord<S>(3), 42u, 3, two, half, 2),
	      tag);
}

} // namespace

int main() {
	std::printf("== HOST-444 noise grid (equivalencia bit a bit) ==\n");
	test_equivalence<MiniFloat16>("equivalencia MiniFloat16");
	test_equivalence<float>("equivalencia float");
	test_cached_shortcuts<MiniFloat16>("atajos MiniFloat16");
	test_cached_shortcuts<float>("atajos float");
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: rejilla de ruido identica a las funciones libres (2 escalares).\n");
	return 0;
}
