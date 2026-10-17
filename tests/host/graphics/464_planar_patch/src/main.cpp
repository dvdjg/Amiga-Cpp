// ============================================================================
// Test HOST-464: generador de parches planares (`eng::graphics/planar_patch.hpp`)
// ============================================================================
//
// Valida el generador promovido al engine (usado por las demos RPG 001/400):
//
//   1) Formato: bloque interleaved 4bpp por fila de pixel (plano0..plano3 de la fila 0,
//      luego la fila 1); bit MSB = pixel izquierdo de la palabra.
//   2) Casos limite: color 0 = palabra a cero; formas con varias palabras por fila (W=32);
//      patrones evaluados en COMPILACION (`static_assert`) y comprobaciones exactas en
//      runtime sobre palabras concretas.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/464_planar_patch

#include <cstdio>

#include <eng/graphics/planar_patch.hpp>

namespace {

using eng::graphics::make_planar_patch;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Solido 16x8 de color 3 (planos 0 y 1 a 1): 4 palabras por fila (4 planos), 32 palabras.
constexpr auto kSolid3 = make_planar_patch<32, 16u, 8u>([](eng::u8, eng::u8) -> eng::u8 {
	return 3u;
});
// Compilacion: fila 0 = planos [0xFFFF, 0xFFFF, 0x0000, 0x0000]; fila 1 igual; color 0 = 0.
static_assert(kSolid3[0] == 0xffffu && kSolid3[1] == 0xffffu && kSolid3[2] == 0u &&
		  kSolid3[3] == 0u,
	      "fila 0: planos 0/1 a 1");
static_assert(kSolid3[4] == 0xffffu, "fila 1 empieza en la palabra 4");

/// Borde 16x16 de color 1 (solo plano 0) en las filas 0 y 15.
constexpr auto kFrame1 = make_planar_patch<64, 16u, 16u>([](eng::u8 y, eng::u8) -> eng::u8 {
	return (y == 0u || y == 15u) ? 1u : 0u;
});
static_assert(kFrame1[0] == 0xffffu && kFrame1[1] == 0u, "borde: plano 0 lleno, resto a 0");
static_assert(kFrame1[4] == 0u && kFrame1[60] == 0xffffu, "borde: filas intermedias a cero");

/// Pixel unico (0,0) de color 12 (planos 2 y 3) en un parche 32x2 (2 palabras por plano).
constexpr auto kDot12 = make_planar_patch<16, 32u, 2u>([](eng::u8 y, eng::u8 x) -> eng::u8 {
	return (y == 0u && x == 0u) ? 12u : 0u;
});
// Indice: (y*4 + plane) * 2 + word. Pixel (0,0): bit MSB de la palabra 0 de los planos 2/3.
static_assert(kDot12[(0u * 4u + 2u) * 2u + 0u] == 0x8000u, "punto: plano 2 palabra 0 MSB");
static_assert(kDot12[(0u * 4u + 3u) * 2u + 0u] == 0x8000u, "punto: plano 3 palabra 0 MSB");
static_assert(kDot12[(0u * 4u + 0u) * 2u + 0u] == 0u, "punto: plano 0 intacto");

void test_runtime_layout() {
	// Pixel (1,9) de color 5 (planos 0 y 2) en un 16x2: palabra 0, bit 0x8000>>9.
	constexpr auto p = make_planar_patch<8, 16u, 2u>([](eng::u8 y, eng::u8 x) -> eng::u8 {
		return (y == 1u && x == 9u) ? 5u : 0u;
	});
	check(p[(1u * 4u + 0u) * 1u] == (0x8000u >> 9u), "runtime: plano 0 con el bit del pixel");
	check(p[(1u * 4u + 2u) * 1u] == (0x8000u >> 9u), "runtime: plano 2 con el bit del pixel");
	check(p[(1u * 4u + 1u) * 1u] == 0u && p[(1u * 4u + 3u) * 1u] == 0u,
	      "runtime: planos no seleccionados a cero");
	// Color 0 = parche vacio.
	constexpr auto empty = make_planar_patch<64, 16u, 16u>([](eng::u8, eng::u8) -> eng::u8 {
		return 0u;
	});
	eng::u16 sum = 0u;
	for (eng::u16 w : empty) {
		sum = static_cast<eng::u16>(sum | w);
	}
	check(sum == 0u, "runtime: color 0 produce un bloque vacio");
}

} // namespace

int main() {
	std::printf("Planar patch (generador constexpr 4bpp interleaved):\n");
	test_runtime_layout();
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: planar patch (layout/limites/compilacion)\n");
	return 0;
}
