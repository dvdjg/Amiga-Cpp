// ============================================================================
// Test HOST-385: mod16 — modulo de Blitter/Copper con asercion de rango.
// ============================================================================
//
// Respalda `eng::graphics::mod16`/`mod16u` (`blit_job.hpp`): convierte un valor de modulo
// calculado en 32 bits a `s16` (registro `BLTxMOD`) comprobando el rango. NO restringe a
// potencias de 2 (los modulos reales no lo son: `40*4 - 42 = 118`); evita el truncado silencioso
// del `static_cast<s16>` a ciegas. En release la asercion desaparece (coste cero).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/385_mod16

#include <cstdio>

#include <eng/graphics/blit_job.hpp>

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
	std::printf("== HOST-385 mod16 ==\n");

	// Modulos tipicos de BOB/playfield: no potencias de 2.
	check(eng::graphics::mod16(40 - 2 * 2) == 36, "dst_mod 40 - 2w = 36");
	check(eng::graphics::mod16(40 - 3 * 2) == 34, "dst_mod 40 - 3w = 34");
	check(eng::graphics::mod16(40 * 4 - 42) == 118, "dst_mod 40*4 - 42 = 118 (no potencia de 2)");
	check(eng::graphics::mod16u(2 * 2) == 4, "src_mod words*2 = 4");

	// Negativos (modulo A tras solapar el patron): caben en s16.
	check(eng::graphics::mod16(-2 * 2) == -4, "modulo negativo -4");
	check(eng::graphics::mod16(-32768) == -32768, "extremo negativo");
	check(eng::graphics::mod16(32767) == 32767, "extremo positivo");

	// En host (sin ENG_AMIGA/ENG_DEBUG) no hay asercion: el valor se trunca como un cast.
	// (El rango que dispara el trap se prueba en m68k/build de diagnostico, no aqui.)
	(void)eng::graphics::mod16(100000);

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: mod16 (modulo de Blitter/Copper con rango) validado.\n");
	return 0;
}
