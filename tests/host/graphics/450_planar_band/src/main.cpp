// ============================================================================
// Test HOST-450: bandas planares (`eng::graphics::planar_band`)
// ============================================================================
//
// Cubre la pieza de parcheo planar del display que usa la demo 121: construir una banda
// desde un patrón de píxeles desplazado (los 16 desplazamientos precomputados), extraer
// y escribir rectángulos de bitplanes (`get_band`/`put_band`) y el **cookie-cut planar**
// con máscara (`put_band_masked`: solo escribe donde la máscara está a 1 — el mecanismo
// con el que un actor respeta la capa de overhead).
//
//   bash tools/run-host-tests.sh tests/host/graphics/450_planar_band

#include <cstdio>
#include <cstring>

#include <eng/graphics/planar_band.hpp>

namespace gfx = eng::graphics;
using eng::u8;
using eng::u16;
using eng::u32;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// ---------------------------------------------------------------------------
//  1. Dimensiones de banda y construcción desde píxeles (con desplazamiento)
// ---------------------------------------------------------------------------

void test_make_band() {
	check(gfx::band_window_words(16u) == 2u, "ventana de 16 px = 2 palabras (shift 0..15)");
	check(gfx::band_window_words(32u) == 3u, "ventana de 32 px = 3 palabras");
	check(gfx::band_words(2u, 16u, 5u) == 160u, "banda 2x16x5 = 160 palabras");

	// Patrón 16×2: (0,0)=1, (15,0)=2, (0,1)=3; el resto transparente.
	eng::u8 pixels[32] {};
	pixels[0] = 1u;
	pixels[15] = 2u;
	pixels[16] = 3u;

	u16 band[gfx::band_words(2u, 2u, 5u)] {};
	u16 mask[2u * 2u] {};
	gfx::make_band_from_pixels<16, 2, 5>(pixels, 0u, band, mask);
	// Bit 15 = píxel izquierdo; color 1 -> plano 0, color 2 -> plano 1, color 3 -> planos 0+1.
	check(band[(0u * 2u + 0u) * 2u + 0u] == 0x8000u, "make_band: (0,0) color1 -> plano0 bit15");
	check(band[(1u * 2u + 0u) * 2u + 0u] == 0x0001u, "make_band: (15,0) color2 -> plano1 bit0");
	check(band[(0u * 2u + 1u) * 2u + 0u] == 0x8000u && band[(1u * 2u + 1u) * 2u + 0u] == 0x8000u,
	      "make_band: (0,1) color3 -> planos 0+1");
	check(mask[0u] == 0x8001u && mask[2u] == 0x8000u, "make_band: mascara de cobertura");
	bool rest_zero = true;
	for (u32 i = 0u; i < gfx::band_words(2u, 2u, 5u); ++i) {
		const bool used = (i % 2u) == 0u; // columna 0 (palabra 0) de cada fila/plano
		if (!used && band[i] != 0u) rest_zero = false;
	}
	check(rest_zero, "make_band: el resto de la banda nace a cero");

	// Desplazamiento 3: (0,0) -> bit 12 de la palabra 0; (15,0) -> palabra 1, bit 13.
	gfx::make_band_from_pixels<16, 2, 5>(pixels, 3u, band, mask);
	check(band[(0u * 2u + 0u) * 2u + 0u] == 0x1000u, "make_band shift3: (0,0) -> bit12");
	check(band[(1u * 2u + 0u) * 2u + 1u] == 0x2000u, "make_band shift3: (15,0) -> palabra 1");
	check(mask[0u] == 0x1000u && mask[1u] == 0x2000u, "make_band shift3: mascara desplazada");
}

// ---------------------------------------------------------------------------
//  2. get_band / put_band sobre un bitmap planar sintético
// ---------------------------------------------------------------------------

void test_get_put() {
	// Bitmap de 3 planos, 4 palabras por fila, 3 filas (plano separado 12 palabras).
	u16 buf[3u * 12u] {};
	for (eng::u8 p = 0u; p < 3u; ++p) {
		for (u16 r = 0u; r < 3u; ++r) {
			for (u16 c = 0u; c < 4u; ++c) {
				buf[p * 12u + r * 4u + c] = u16(0x1100u * (p + 1u) + r * 16u + c);
			}
		}
	}
	u16 band[gfx::band_words(2u, 2u, 3u)] {};
	// row_bytes = 8 B (4 palabras); plane_stride = 24 B (12 palabras).
	gfx::get_band(reinterpret_cast<const u8*>(buf), 24u, 8u, 1u, 1u, 2u, 2u, 3u, band);
	bool match = true;
	for (eng::u8 p = 0u; p < 3u; ++p) {
		for (u16 r = 0u; r < 2u; ++r) {
			for (u16 c = 0u; c < 2u; ++c) {
				match = match &&
					band[(p * 2u + r) * 2u + c] == buf[p * 12u + (1u + r) * 4u + 1u + c];
			}
		}
	}
	check(match, "get_band copia el rectangulo (palabra/plano/fila)");

	u16 out[3u * 12u] {};
	gfx::put_band(reinterpret_cast<u8*>(out), 24u, 8u, 1u, 1u, 2u, 2u, 3u, band);
	bool placed = true;
	bool others_zero = true;
	for (eng::u8 p = 0u; p < 3u; ++p) {
		for (u16 r = 0u; r < 3u; ++r) {
			for (u16 c = 0u; c < 4u; ++c) {
				const u16 v = out[p * 12u + r * 4u + c];
				if (r >= 1u && r <= 2u && c >= 1u && c <= 2u) {
					placed = placed &&
						 v == band[(p * 2u + (r - 1u)) * 2u + (c - 1u)];
				} else if (v != 0u) {
					others_zero = false;
				}
			}
		}
	}
	check(placed, "put_band escribe la banda en (wx=1, y=1)");
	check(others_zero, "put_band no toca fuera del rectangulo");
}

// ---------------------------------------------------------------------------
//  3. put_band_masked: cookie-cut y la ley «actor oculto por la capa de overhead»
// ---------------------------------------------------------------------------

/// Referencia **bit a bit** (independiente): recorre las 16 posiciones de la palabra y
/// elige el bit de `src` donde la máscara lo pide, y el de `bg` donde no.
u16 reference_masked(u16 bg, u16 src, u16 mask) {
	u16 ref = 0u;
	for (eng::u8 b = 0u; b < 16u; ++b) {
		const u16 bit = u16(0x8000u >> b);
		ref = u16(ref | ((mask & bit) != 0u ? (src & bit) : (bg & bit)));
	}
	return ref;
}

void test_masked() {
	// 1 plano, 1 palabra por fila, 2 filas.
	u16 dst[2] = {0xAAAAu, 0x5555u};
	const u16 src[2] = {0x1234u, 0x5678u};
	// La ley de la demo 121: máscara del actor (=patrón) & ~máscara de copas.
	const u16 actor_mask[2] = {0x3C3Cu, 0xFFFFu};
	const u16 canopy_mask[2] = {0x0FF0u, 0x00FFu};
	u16 final_mask[2] {};
	for (u16 r = 0u; r < 2u; ++r) {
		final_mask[r] = u16(actor_mask[r] & u16(~canopy_mask[r]));
	}
	const u16 expect0 = reference_masked(dst[0], src[0], final_mask[0]);
	const u16 expect1 = reference_masked(dst[1], src[1], final_mask[1]);
	gfx::put_band_masked(reinterpret_cast<u8*>(dst), 2u, 2u, 0u, 0u, 1u, 2u, 1u, src,
			     final_mask);
	check(dst[0] == expect0 && dst[1] == expect1,
	      "put_band_masked = (dst & ~m) | (src & m) contra referencia bit a bit");
	// Bajo la copa manda el fondo; fuera, el actor.
	check((dst[0] & 0x0FF0u) == (0xAAAAu & 0x0FF0u), "bajo la copa se conserva el fondo");
	check((dst[0] & final_mask[0]) == (src[0] & final_mask[0]),
	      "fuera de la copa se pinta el actor");

	// Máscara a cero: el destino queda intacto.
	const u16 zeros[2] = {0u, 0u};
	const u16 before[2] = {0x1234u, 0xABCDu};
	dst[0] = before[0];
	dst[1] = before[1];
	gfx::put_band_masked(reinterpret_cast<u8*>(dst), 2u, 2u, 0u, 0u, 1u, 2u, 1u, src, zeros);
	check(dst[0] == before[0] && dst[1] == before[1], "mascara 0 deja el destino intacto");
}

} // namespace

int main() {
	std::printf("== HOST-450 planar bands (parcheo planar del display) ==\n");
	test_make_band();
	test_get_put();
	test_masked();
	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: bandas planares validadas (construccion, get/put y cookie-cut).\n");
	return 0;
}
