// ============================================================================
// Test HOST-427: cocinado de la DATA de un par de sprites *attached* (15 colores).
// ============================================================================
//
// Valida en host el módulo puro `eng/graphics/sprite_attached.hpp`
// (`cook_attached_pair`): divide un bitmap planar de 4 planos y 16 px en las dos
// estructuras DMA del par *attached*, `[POS, CTL, DAT/DATB…, 0,0]` (canal par =
// planos 0-1; canal impar = planos 2-3 con el bit 7 ATTACH forzado). La unidad de
// DMA es la estructura con cabecera (`docs/reference/emulators/winuae/sprite-dma.md`).
// Referencia de color: AHRM 3.ª cap. 4, «Attached Sprites» y Table 4-5;
// `docs/reference/amiga/techniques/sprite-layer.md` §4.
//
// Las estructuras de salida se pasan como `ChipView<SpriteTag>` (`MemView` con el
// banco en el tipo): son DMA, no una `Span` agnóstica (AGENTS §1.10). En host el
// `ChipView` envuelve un array local; el tipo es lo que se valida.
//
// Comprobaciones:
//   1) `attached_pair_structure_words`: cabecera + DATA + terminador.
//   2) Rechazos: altura 0, punteros/vistas cortas, y sin escritura al rechazar.
//   3) Cabeceras: POS igual en ambos canales; ATTACH (bit 7) solo en el impar.
//   4) Round-trip por píxel: el índice de 4 bits se reconstruye del par y coincide
//      con el del bitmap de 4 planos (bit 15 = píxel 0).
//   5) Terminador de DMA a cero al final de cada estructura.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/427_sprite_attached   (solo este)
//   bash tools/run-host-tests.sh                                           (todos)

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/graphics/sprite_attached.hpp>

namespace {

using eng::graphics::attached_pair_structure_words;
using eng::graphics::cook_attached_pair;

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

/// Vista Chip sobre un array local (en host el banco es solo tipo; la cocina es CPU).
eng::ChipView<eng::SpriteTag> chip(eng::u16* p, eng::usize words) {
	return eng::ChipView<eng::SpriteTag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(p), words * 2u};
}

template <eng::usize N>
eng::ChipView<eng::SpriteTag> chip(eng::u16 (&arr)[N]) {
	return chip(arr, N);
}

/// Bit del píxel `px` (0 = izquierda = bit 15) de una word de sprite.
constexpr eng::u16 pixel_bit(eng::u16 word, eng::u16 px) {
	return static_cast<eng::u16>((word >> (15u - px)) & 1u);
}

void test_structure_words() {
	std::printf("attached_pair_structure_words: cabecera + DATA + terminador\n");
	CHECK(attached_pair_structure_words(1u) == 6u);
	CHECK(attached_pair_structure_words(16u) == 36u);
	CHECK(attached_pair_structure_words(24u) == 52u);
}

void test_rejects() {
	std::printf("cook: rechaza entradas cortas sin escribir\n");

	eng::u16 src[8] {}; // 2 filas de 4 planos = 8 words
	eng::u16 even[8] {};
	eng::u16 odd[8] {};
	eng::u16 small[5] {};

	// Altura 0.
	CHECK(!cook_attached_pair(eng::Span<const eng::u16> {src, 8u}, 0u, 0x1000u, 0u,
				  chip(even), chip(odd)));

	// Fuente corta (2 filas -> 8 words; se piden 12).
	CHECK(!cook_attached_pair(eng::Span<const eng::u16> {src, 8u}, 3u, 0x1000u, 0u,
				  chip(even), chip(odd)));

	// Salida corta: 2 filas -> 16 bytes por estructura; el impar solo tiene 10.
	CHECK(!cook_attached_pair(eng::Span<const eng::u16> {src, 8u}, 2u, 0x1000u, 0u,
				  chip(even), chip(small)));

	// Vistas nulas.
	CHECK(!cook_attached_pair(eng::Span<const eng::u16> {}, 2u, 0x1000u, 0u,
				  chip(even), chip(odd)));

	// Sin escritura al rechazar.
	eng::u16 guard_even[8];
	eng::u16 guard_odd[8];
	for (eng::u16 i = 0; i < 8u; ++i) {
		guard_even[i] = 0xababu;
		guard_odd[i] = 0xcdcdu;
	}
	CHECK(!cook_attached_pair(eng::Span<const eng::u16> {src, 4u}, 4u, 0x1000u, 0u,
				  chip(guard_even), chip(guard_odd)));
	CHECK(guard_even[0] == 0xababu && guard_odd[7] == 0xcdcdu);

	// Una valida si entra.
	CHECK(cook_attached_pair(eng::Span<const eng::u16> {src, 8u}, 2u, 0x1000u, 0x0700u,
				 chip(even), chip(odd)));
}

void test_headers() {
	std::printf("cook: cabecera POS igual y ATTACH solo en el canal impar\n");

	constexpr eng::u16 kHeight = 3u;
	eng::u16 src[4u * kHeight] {};
	eng::u16 even[attached_pair_structure_words(kHeight)] {};
	eng::u16 odd[attached_pair_structure_words(kHeight)] {};

	// POS del par: VSTART=0x30, HSTART=0x80 (px 128); CTL: VSTOP=0x33, sin flags.
	CHECK(cook_attached_pair(eng::Span<const eng::u16> {src, 4u * kHeight}, kHeight, 0x3040u,
				 0x3300u, chip(even), chip(odd)));

	CHECK(even[0] == 0x3040u && odd[0] == 0x3040u); // misma POS
	CHECK(even[1] == 0x3300u);                      // par: sin ATTACH
	CHECK(odd[1] == 0x3380u);                       // impar: bit 7 ATTACH
	CHECK((even[1] & 0x0080u) == 0u && (odd[1] & 0x0080u) != 0u);
}

void test_roundtrip() {
	std::printf("cook: round-trip del indice de 4 bits por pixel\n");

	constexpr eng::u16 kHeight = 9u;
	constexpr eng::u16 kWordsPerPlane = kHeight; // 16 px = 1 word por fila

	// Fuente: 4 planos contiguos; indice del pixel = (px + 3*row) & 15.
	eng::u16 src[4u * kHeight] {};
	for (eng::u16 row = 0; row < kHeight; ++row) {
		for (eng::u16 px = 0; px < 16u; ++px) {
			const eng::u16 v = static_cast<eng::u16>((px + 3u * row) & 0xfu);
			if ((v & 1u) != 0u) src[row] |= static_cast<eng::u16>(0x8000u >> px);
			if ((v & 2u) != 0u) src[kWordsPerPlane + row] |= static_cast<eng::u16>(0x8000u >> px);
			if ((v & 4u) != 0u) src[kWordsPerPlane * 2u + row] |= static_cast<eng::u16>(0x8000u >> px);
			if ((v & 8u) != 0u) src[kWordsPerPlane * 3u + row] |= static_cast<eng::u16>(0x8000u >> px);
		}
	}

	eng::u16 even[attached_pair_structure_words(kHeight)] {};
	eng::u16 odd[attached_pair_structure_words(kHeight)] {};
	CHECK(cook_attached_pair(eng::Span<const eng::u16> {src, 4u * kHeight}, kHeight, 0x2c10u,
				 0x2e00u, chip(even), chip(odd)));

	for (eng::u16 row = 0; row < kHeight; ++row) {
		const eng::u16 dat_even = even[2u + row * 2u];
		const eng::u16 datb_even = even[2u + row * 2u + 1u];
		const eng::u16 dat_odd = odd[2u + row * 2u];
		const eng::u16 datb_odd = odd[2u + row * 2u + 1u];
		for (eng::u16 px = 0; px < 16u; ++px) {
			const eng::u16 v = static_cast<eng::u16>(
				pixel_bit(dat_even, px) | (pixel_bit(datb_even, px) << 1u) |
				(pixel_bit(dat_odd, px) << 2u) | (pixel_bit(datb_odd, px) << 3u));
			const eng::u16 expect = static_cast<eng::u16>((px + 3u * row) & 0xfu);
			if (v != expect) {
				std::printf("  [FAIL] row %u px %u: v=%u expect=%u (linea %d)\n", row, px,
					    v, expect, __LINE__);
				++g_failures;
			}
		}
	}

	// Terminador de DMA a cero en ambas estructuras.
	const eng::u16 term = static_cast<eng::u16>(attached_pair_structure_words(kHeight) - 2u);
	CHECK(even[term] == 0u && even[term + 1u] == 0u);
	CHECK(odd[term] == 0u && odd[term + 1u] == 0u);
}

void test_plane_mapping() {
	std::printf("cook: canal par = planos 0-1; impar = planos 2-3\n");

	constexpr eng::u16 kHeight = 2u;
	eng::u16 src[4u * kHeight] {};
	// Fila 0: solo el plano 2 (bit 2 -> DAT del canal IMPAR).
	src[kHeight * 2u + 0u] = 0x8001u;
	// Fila 1: solo el plano 1 (bit 1 -> DATB del canal PAR).
	src[kHeight + 1u] = 0x00ffu;

	eng::u16 even[attached_pair_structure_words(kHeight)] {};
	eng::u16 odd[attached_pair_structure_words(kHeight)] {};
	CHECK(cook_attached_pair(eng::Span<const eng::u16> {src, 4u * kHeight}, kHeight, 0u, 0u,
				 chip(even), chip(odd)));

	// Plano 2 -> DAT del impar (fila 0); los demas streams a cero.
	CHECK(odd[2u] == 0x8001u && odd[3u] == 0u);
	CHECK(even[2u] == 0u && even[3u] == 0u);
	// Plano 1 -> DATB del par (fila 1).
	CHECK(even[4u] == 0u && even[5u] == 0x00ffu);
	CHECK(odd[4u] == 0u && odd[5u] == 0u);
}

} // namespace

int main() {
	std::printf("Test HOST-427 sprite_attached (estructuras DMA de un par attached)\n");
	std::printf("==================================================================\n");

	test_structure_words();
	test_rejects();
	test_headers();
	test_roundtrip();
	test_plane_mapping();

	if (g_failures == 0) {
		std::printf("OK: par attached (estructuras DMA de 4 planos) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
