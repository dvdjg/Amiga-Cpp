// ============================================================================
// Test HOST-009: tablas de onda (eng::audio::sine_byte/triangle_byte/square_byte).
// ============================================================================
//
// Valida en host las tablas de forma de onda del engine (enteras, sin float):
// que un ciclo de 64 muestras cubra el rango ±127, tenga media ~0 y la simetría
// esperada (el seno pasa por 0 en los cuartos del ciclo).

#include <cstdio>

#include <eng/audio/wave_tables.hpp>
#include <eng/core/types.hpp>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

template <eng::s8 (*Fn)(eng::u32)>
void check_wave(const char* name) {
	std::printf("wave: %s 64 muestras, rango ±127, media ~0\n", name);
	eng::s16 mn = 0, mx = 0;
	eng::s32 sum = 0;
	for (eng::u32 i = 0; i < 64; ++i) {
		const eng::s16 v = Fn(i);
		if (v < mn) mn = v;
		if (v > mx) mx = v;
		sum += v;
	}
	CHECK(mn <= -120 && mn >= -127);
	CHECK(mx >= 120 && mx <= 127);
	CHECK(sum >= -8 && sum <= 8);
}

void test_sine_symmetry() {
	std::printf("wave: seno pasa por 0 en los cuartos del ciclo\n");
	CHECK(eng::audio::sine_byte(0) == 0);
	CHECK(eng::audio::sine_byte(32) == 0);
	CHECK(eng::audio::sine_byte(16) == 127);
	CHECK(eng::audio::sine_byte(48) == -127);
	CHECK(eng::audio::sine_byte(8) == 90);
}

} // namespace

int main() {
	std::printf("Test HOST-009 wave_tables\n");
	std::printf("==========================\n");

	check_wave<eng::audio::sine_byte>("sine");
	check_wave<eng::audio::triangle_byte>("triangle");
	check_wave<eng::audio::square_byte>("square");
	test_sine_symmetry();

	if (g_failures == 0) {
		std::printf("OK: tablas de onda validadas (seno/triangular/cuadrada).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
