// ============================================================================
// Test HOST-474: patrones de relleno de polígonos (eng::graphics::FillPattern)
// ============================================================================
//
// Valida el catálogo de patrones (`eng/graphics/fill_pattern.hpp`) y su aplicación en el
// relleno por bitplane:
//
//   1) Palabras del catálogo: goldens de tablero (2×2 y 4×4), punteado, líneas
//      verticales/horizontales y oblicuas 45°/135° (periodo y continuidad).
//   2) Plano de patrón (`fill_pattern_plane_cpu`): filas = palabra repetida a lo ancho,
//      altura recortada a la capacidad del buffer.
//   3) Composición por plano (`fill_polygons_by_plane_cpu` + patrón): para un rectángulo,
//      el resultado es `dentro(x,y) && bit_del_patrón(x,y)` verificado **bit a bit** contra
//      una referencia independiente por píxel; y `Solid` reproduce el relleno sin patrón.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/474_fill_patterns

#include <eng/graphics/fill_pattern.hpp>
#include <eng/graphics/polygon_planes.hpp>

#include <cstdio>

namespace {

using eng::u8;
using eng::u16;
using eng::u32;
using eng::graphics::FillPattern;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_words() {
	using eng::graphics::fill_pattern_word;
	// Sólido.
	for (u32 y = 0; y < 20; ++y) {
		check(fill_pattern_word(FillPattern::Solid, y) == 0xffffu, "Solid = 0xFFFF");
	}
	// Tablero 2x2: filas alternas 0xAAAA/0x5555 (MSB = píxel 0).
	check(fill_pattern_word(FillPattern::Checker2, 0) == 0xaaaau, "Checker2 y=0");
	check(fill_pattern_word(FillPattern::Checker2, 1) == 0x5555u, "Checker2 y=1");
	check(fill_pattern_word(FillPattern::Checker2, 2) == 0xaaaau, "Checker2 periodo 2");
	// Tablero 4x4: cada fila par con las columnas 0-1 (0xCCCC) y las impares 0x3333,
	// repitiendo el par a las 4 filas.
	check(fill_pattern_word(FillPattern::Checker4, 0) == 0xccccu, "Checker4 y=0");
	check(fill_pattern_word(FillPattern::Checker4, 2) == 0x3333u, "Checker4 y=2");
	check(fill_pattern_word(FillPattern::Checker4, 4) == 0xccccu, "Checker4 periodo 4");
	// Punteado y líneas.
	check(fill_pattern_word(FillPattern::Dots2, 0) == 0x8888u, "Dots2 fila activa");
	check(fill_pattern_word(FillPattern::Dots2, 1) == 0x0000u, "Dots2 fila vacía");
	check(fill_pattern_word(FillPattern::VLines4, 7) == 0x8888u, "VLines4 constante");
	check(fill_pattern_word(FillPattern::HLines4, 0) == 0xffffu, "HLines4 y=0");
	check(fill_pattern_word(FillPattern::HLines4, 3) == 0x0000u, "HLines4 y=3");
	// Oblicuas: rotación de bit por fila, periodo 16.
	check(fill_pattern_word(FillPattern::Oblique45, 0) == 0x8000u, "Oblique45 y=0");
	check(fill_pattern_word(FillPattern::Oblique45, 1) == 0x4000u, "Oblique45 y=1");
	check(fill_pattern_word(FillPattern::Oblique45, 15) == 0x0001u, "Oblique45 y=15");
	check(fill_pattern_word(FillPattern::Oblique45, 16) == 0x8000u, "Oblique45 periodo 16");
	bool shl_ok = true;
	for (u32 y = 0; y < 40; ++y) {
		const u16 w = fill_pattern_word(FillPattern::Oblique135, y);
		if (w != static_cast<u16>(0x0001u << (y & 15u))) {
			shl_ok = false;
		}
	}
	check(shl_ok, "Oblique135 = 0x0001 << (y mod 16)");
	check(eng::graphics::fill_pattern_period(FillPattern::Checker2) == 2u, "periodo Checker2");
	check(eng::graphics::fill_pattern_period(FillPattern::Oblique45) == 16u, "periodo Oblique45");
	check(eng::graphics::fill_pattern_period(FillPattern::Solid) == 1u, "periodo Solid");
}

void test_plane_build() {
	// 32 px (4 bytes) x 8 filas; pedimos 12 filas con capacidad para 8: se recorta.
	// Se compara por palabras u16 (endian-agnóstico: el bit 15 es el píxel izquierdo).
	u8 mem[4u * 8u] = {};
	eng::MaskBuffer plane {mem};
	eng::graphics::fill_pattern_plane_cpu(plane, 4u, 12u, FillPattern::Checker2);
	const u16* rows = reinterpret_cast<const u16*>(mem);
	bool rows_ok = true;
	for (u32 y = 0; y < 8u; ++y) {
		const u16 want = (y & 1u) != 0u ? 0x5555u : 0xaaaau;
		for (u32 w = 0; w < 2u; ++w) {
			if (rows[y * 2u + w] != want) {
				rows_ok = false;
			}
		}
	}
	check(rows_ok, "plano de patrón: filas = palabra repetida, altura recortada");
	// Oblique45 en el mismo buffer: cada fila tiene su palabra según y.
	u8 mem2[2u * 4u] = {};
	eng::MaskBuffer plane2 {mem2};
	eng::graphics::fill_pattern_plane_cpu(plane2, 2u, 4u, FillPattern::Oblique45);
	const u16* r2 = reinterpret_cast<const u16*>(mem2);
	check(r2[0] == 0x8000u, "Oblique45 fila 0 = 0x8000");
	check(r2[1] == 0x4000u, "Oblique45 fila 1 = 0x4000");
	check(r2[3] == 0x1000u, "Oblique45 fila 3 = 0x1000");
}

/// Referencia independiente: ¿el píxel `(x, y)` cae dentro del rectángulo
/// `[x0, x1) x [y0, y1)` y el patrón lo deja pasar?
bool expected_pixel(FillPattern kind, u32 x, u32 y, u32 x0, u32 y0, u32 x1, u32 y1) {
	if (x < x0 || x >= x1 || y < y0 || y >= y1) {
		return false;
	}
	switch (kind) {
	case FillPattern::Solid:
		return true;
	case FillPattern::Checker2:
		return ((x + y) & 1u) == 0u;
	case FillPattern::Checker4:
		return ((x & 2u) == (y & 2u));
	case FillPattern::Dots2:
		return (y & 1u) == 0u && (x & 3u) == 0u;
	case FillPattern::VLines4:
		return (x & 3u) == 0u;
	case FillPattern::HLines4:
		return (y & 3u) == 0u;
	case FillPattern::Oblique45:
		return ((x - y) & 15u) == 0u;
	case FillPattern::Oblique135:
		return ((x + y) & 15u) == 15u; // bit 15-k con k = y%16 -> x = 15-(y%16)
	}
	return false;
}

void test_compose(FillPattern kind, const char* name) {
	// Plano de 64 px (8 bytes) x 12 filas x 2 planos; rectángulo (x 4..35, y 2..9) con
	// color 3: bits 0 y 1 -> se rellena en ambos planos.
	constexpr u32 kRowBytes = 8u;
	constexpr u32 kRows = 12u;
	constexpr u32 kPlaneBytes = kRowBytes * kRows;
	u8 fb[2u * kPlaneBytes] = {};
	eng::PlaneBytes dest {fb};

	const eng::s16 xs16[4] = {4, 36, 36, 4};
	const eng::s16 ys16[4] = {2, 2, 10, 10};
	const eng::graphics::PlanePolygon face {xs16, ys16, 4u, 3u};
	const eng::Span<const eng::graphics::PlanePolygon> faces {&face, 1u};
	const u32 spans = eng::graphics::fill_polygons_by_plane_cpu(faces, dest, kRowBytes,
								    kPlaneBytes, 2u, 64u, kRows, kind);
	check(spans > 0u, "composición con patrón produce spans");

	bool bits_ok = true;
	for (u32 p = 0; p < 2u; ++p) {
		for (u32 y = 0; y < kRows; ++y) {
			for (u32 x = 0; x < 64u; ++x) {
				const u8 byte = fb[p * kPlaneBytes + y * kRowBytes + (x >> 3u)];
				const bool got = ((byte >> (7u - (x & 7u))) & 1u) != 0u;
				const bool want = expected_pixel(kind, x, y, 4u, 2u, 36u, 10u);
				if (got != want) {
					bits_ok = false;
				}
			}
		}
	}
	char msg[128];
	std::snprintf(msg, sizeof(msg), "composición %s: rectángulo ∩ patrón bit a bit", name);
	check(bits_ok, msg);
}

void test_solid_matches_polygon() {
	// Sin patrón (Solid por defecto): el relleno es exactamente el rectángulo (regresión
	// de la firma anterior, que no llevaba patrón).
	test_compose(FillPattern::Solid, "Solid");
}

void test_dither_planes() {
	// Damero 1x1 entre los indices (1,2): plano 0 = damero (A en pares), plano 1 = inverso
	// (B en pares); indices con bits iguales -> plano solido.
	u8 mem[4u * 4u] = {}; // 2 planos x 2 filas x 4 bytes
	eng::MaskBuffer planes {mem};
	check(eng::graphics::fill_dither_plane_cpu(planes, 4u, 1u, 2u, 0u), "dither plano 0 ok");
	check(eng::graphics::fill_dither_plane_cpu(planes.subspan(8u, 8u), 4u, 1u, 2u, 1u),
	      "dither plano 1 ok");
	const u16* w = reinterpret_cast<const u16*>(mem);
	check(w[0] == 0xaaaau && w[1] == 0xaaaau, "dither A/B: plano 0 fila 0 = 0xAAAA");
	check(w[2] == 0x5555u && w[3] == 0x5555u, "dither A/B: plano 0 fila 1 = 0x5555");
	check(w[4] == 0x5555u && w[5] == 0x5555u, "dither A/B: plano 1 fila 0 = 0x5555 (inverso)");
	check(w[6] == 0xaaaau && w[7] == 0xaaaau, "dither A/B: plano 1 fila 1 = 0xAAAA");
	// Par con bits iguales en ese plano (0,1): plano 0 solido por el bit 0 de B; plano 2
	// solido a 0.
	u8 mem2[4u * 3u] = {};
	eng::MaskBuffer p2 {mem2};
	check(eng::graphics::fill_dither_plane_cpu(p2, 4u, 0u, 1u, 0u), "dither (0,1) plano 0 ok");
	const u16* w2 = reinterpret_cast<const u16*>(mem2);
	check(w2[0] == 0x5555u && w2[1] == 0x5555u, "dither (0,1): plano 0 fila 0 = 0x5555");
	check(w2[2] == 0xaaaau, "dither (0,1): plano 0 fila 1 = 0xAAAA");
	// Buffer corto (1 fila) -> rechazo.
	u8 mem3[4u] = {};
	eng::MaskBuffer p3 {mem3};
	check(!eng::graphics::fill_dither_plane_cpu(p3, 4u, 1u, 2u, 0u), "dither rechaza buffer corto");
	// Periodo del inverso y del damero.
	check(eng::graphics::fill_pattern_period(FillPattern::Checker2Inv) == 2u, "periodo Checker2Inv");
	check(eng::graphics::fill_pattern_word(FillPattern::Checker2Inv, 0) == 0x5555u,
	      "Checker2Inv y=0");
	check(eng::graphics::fill_pattern_word(FillPattern::Checker2Inv, 1) == 0xaaaau,
	      "Checker2Inv y=1");
}

void test_compose_dither() {
	// Composicion CPU con patron POR PLANO: rectangulo de color 3 sobre 2 planos con
	// {Checker2, Checker2Inv} = mezcla de los indices (1,2): pixel (x+y) par -> 1, impar -> 2.
	constexpr u32 kRowBytes = 8u;
	constexpr u32 kRows = 12u;
	constexpr u32 kPlaneBytes = kRowBytes * kRows;
	u8 fb[2u * kPlaneBytes] = {};
	eng::PlaneBytes dest {fb};
	const eng::s16 xs16[4] = {4, 36, 36, 4};
	const eng::s16 ys16[4] = {2, 2, 10, 10};
	const eng::graphics::PlanePolygon face {xs16, ys16, 4u, 3u};
	const eng::Span<const eng::graphics::PlanePolygon> faces {&face, 1u};
	const FillPattern pats[2] = {FillPattern::Checker2, FillPattern::Checker2Inv};
	const u32 spans = eng::graphics::fill_polygons_by_plane_patterns_cpu(
		faces, dest, kRowBytes, kPlaneBytes, 2u, 64u, kRows,
		eng::Span<const FillPattern>(pats, 2u));
	check(spans > 0u, "composicion dither produce spans");
	bool ok = true;
	for (u32 y = 0; y < kRows; ++y) {
		for (u32 x = 0; x < 64u; ++x) {
			const bool inside = (x >= 4u && x < 36u && y >= 2u && y < 10u);
			const u8 want = !inside ? 0u : (((x + y) & 1u) == 0u ? 1u : 2u);
			u8 got = 0u;
			for (u32 p = 0; p < 2u; ++p) {
				const u8 byte = fb[p * kPlaneBytes + y * kRowBytes + (x >> 3u)];
				got = static_cast<u8>(got |
						      (((byte >> (7u - (x & 7u))) & 1u) << p));
			}
			if (got != want) {
				ok = false;
			}
		}
	}
	check(ok, "dither (1,2): par -> indice 1, impar -> indice 2, bit a bit");
}

} // namespace

int main() {
	test_words();
	test_plane_build();
	test_compose(FillPattern::Checker2, "Checker2");
	test_compose(FillPattern::Checker4, "Checker4");
	test_compose(FillPattern::VLines4, "VLines4");
	test_compose(FillPattern::HLines4, "HLines4");
	test_compose(FillPattern::Oblique45, "Oblique45");
	test_compose(FillPattern::Dots2, "Dots2");
	test_solid_matches_polygon();

	if (g_fail == 0) {
		std::printf("OK: patrones de relleno (checker, punteado, líneas v/h y oblicuas)\n");
	} else {
		std::printf("FALLOS: %u\n", g_fail);
	}
	return g_fail == 0 ? 0 : 1;
}
