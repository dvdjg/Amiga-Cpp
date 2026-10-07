// ============================================================================
// Test HOST-009: tablas de onda (eng::audio::sine_byte/triangle_byte/square_byte)
// y generación de muestras (sine_wave<T>/synth_tone<T>).
// ============================================================================
//
// Valida en host las tablas de forma de onda del engine (enteras, sin float):
// que un ciclo de 64 muestras cubra el rango ±127, tenga media ~0 y la simetría
// esperada (el seno pasa por 0 en los cuartos del ciclo). Además, que el seno sea
// genérico sobre el tipo de muestra (s16 con su pico) y que `synth_tone` escriba
// u8 (Paula) o s16 con la amplitud pedida.

#include <cstdio>

#include <eng/audio/wave_tables.hpp>
#include <eng/core/types/types.hpp>

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

	// La tabla generada (serie de Taylor redondeada) reproduce byte a byte la histórica.
	{
		static const eng::s8 kRef[17] = {0,  12,  25,  37,  49,  60,  71,  81,  90,
						 98, 106, 112, 117, 122, 125, 126, 127};
		bool exact = true;
		for (eng::u32 i = 0; i < 17u; ++i) {
			exact = exact && eng::audio::sine_byte(i) == kRef[i];
		}
		CHECK(exact);
	}

	// El seno es genérico sobre el tipo de muestra: s16 usa su pico completo.
	CHECK(eng::audio::sine_wave<eng::s16>(0u) == 0);
	CHECK(eng::audio::sine_wave<eng::s16>(16u) == 32767);
	CHECK(eng::audio::sine_wave<eng::s16>(48u) == -32767);

	// `synth_tone` escribe el tipo de muestra pedido con la amplitud dada.
	{
		eng::u8 b8[64] = {};
		eng::audio::synth_tone<64>(b8, 1000u, 44100u, 100);
		CHECK(b8[0] == 0u && b8[16] == 100u && b8[48] == 156u); // -100 con signo en byte

		eng::s16 b16[64] = {};
		eng::audio::synth_tone<64>(b16, 1000u, 44100u, 30000);
		CHECK(b16[0] == 0 && b16[16] == 30000 && b16[48] == -30000);
	}

	if (g_failures == 0) {
		std::printf("OK: tablas de onda validadas (seno/triangular/cuadrada).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
