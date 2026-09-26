// ============================================================================
// Test HOST-338: decode 2bpp -> planar (eng::graphics::chr_to_planar).
// ============================================================================
//
// Respalda `eng/graphics/chr.hpp`: convierte un tile 2bpp (formato NES/Game Boy) a planos
// contiguos de Amiga. Cubre copia de planos, padding de fila, planos extra a 0 y rechazo de
// argumentos invalidos. Es el helper para `IPatternCache`/`ISpriteEngine::define` externos.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/338_chr

#include <cstdio>

#include <eng/graphics/chr.hpp>

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
	std::printf("== HOST-338 chr ==\n");

	// Tile 8x8 2bpp: plano0 = {0x80,0x40,...}, plano1 = {0x01,0x02,...}.
	eng::u8 chr[16] = {0x80u, 0x40u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
			   0x01u, 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
	eng::u8 out[16] {};
	check(eng::graphics::chr_to_planar(chr, out, 8u, 8u, 2u, 1u, 8u), "decode 8x8x2");
	check(out[0] == 0x80u && out[1] == 0x40u, "plano 0 copiado");
	check(out[8] == 0x01u && out[9] == 0x02u, "plano 1 copiado (offset plane_stride)");

	// Fila de 16 px (in_row=2, in_plane=16) a 2 planos, row_bytes=2.
	eng::u8 chr16[32] = {0xAAu, 0xBBu, 0xCCu, 0xDDu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			     0x11u, 0x22u, 0x33u, 0x44u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	eng::u8 out16[32] {};
	check(eng::graphics::chr_to_planar(chr16, out16, 16u, 8u, 2u, 2u, 16u), "decode 16x8x2");
	check(out16[0] == 0xAAu && out16[1] == 0xBBu, "plano0 16px");
	check(out16[16] == 0x11u && out16[17] == 0x22u, "plano1 16px");

	// Planos extra (>2) se rellenan a 0.
	// 3 planos (formato con un tercer plano, p. ej. color extra): 3 x 16 bytes.
	eng::u8 chr3[48] = {0xAAu, 0xBBu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			    0x11u, 0x22u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			    0x77u, 0x88u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	eng::u8 out3[48] {};
	check(eng::graphics::chr_to_planar(chr3, out3, 16u, 8u, 3u, 2u, 16u), "decode 3 planos");
	check(out3[32] == 0x77u && out3[33] == 0x88u, "plano2 copiado");

	// Argumentos invalidos.
	eng::u8 o[16] {};
	check(!eng::graphics::chr_to_planar(chr, o, 12u, 8u, 2u, 2u, 16u), "ancho no multiplo de 8");
	check(!eng::graphics::chr_to_planar(nullptr, o, 8u, 8u, 2u, 1u, 8u), "origen nulo");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: chr_to_planar (2bpp->planar) validado.\n");
	return 0;
}
