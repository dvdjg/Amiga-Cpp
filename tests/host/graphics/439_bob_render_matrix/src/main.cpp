// ============================================================================
// Test HOST-439: matriz de render de BOBs — ejecutor SOFTWARE del FramePlan.
// ============================================================================
//
// Respalda "todos los usos de bob" del motor: Or ($FC), Opaque ($F0), And ($C0),
// cookie-cut planar con máscara separada ($CA), cookie-cut interleaved "par" (un solo
// blit), layout planar e interleaved, desplazamiento fino (barrel), padding de FastCopy,
// erase box (ClearRect), save-under (CopyRect/RestoreRect), clip por la izquierda y
// bob de 4 planos. Cada caso ejecuta el plan con `tests/host/support/software_blitter.hpp`
// (réplica del encoder `blitter_job_from` + backend `amiga_blitter.cpp`) y comprueba
// PÍXELES EXACTOS y canarios de memoria (detección de escrituras fuera de fila/plano).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/439_bob_render_matrix

#include <cstdio>
#include <cstring>

#include "../../../support/software_blitter.hpp"

#include <eng/graphics/bob.hpp>

namespace {

int g_fail = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_fail;                                                      \
		}                                                                 \
	} while (0)

// ─── Bitmap destino: 128x64, 4 planos, planar contiguo ──────────────────────
constexpr eng::u16 kRowBytes = 16u;  // 128 px
constexpr eng::u16 kHeight = 64u;
constexpr eng::u8 kPlanes = 4u;
constexpr eng::u32 kPlaneBytes = kRowBytes * kHeight;

alignas(2) eng::u16 g_canary_lo[4] = {0xdeadu, 0xbeefu, 0xdeadu, 0xbeefu};
alignas(2) eng::u8 g_bitmap[kPlaneBytes * kPlanes] {};
/// Destino interleaved (dual-playfield): 2 planos (el BOB interleaved cubre TODOS los
/// planos del destino en un pase; `bob.planes` debe igualar `t.plane_count`).
alignas(2) eng::u8 g_bitmap_il[kPlaneBytes * 2u] {};
alignas(2) eng::u16 g_canary_hi[4] = {0xcafeu, 0xbabeu, 0xcafeu, 0xbabeu};

alignas(2) eng::u8 g_sheet[16384] {};  // hoja principal (planar/interleaved/par)
alignas(2) eng::u8 g_mask[8192] {};    // máscara planar separada
alignas(2) eng::u8 g_save[8192] {};    // buffer save-under

// ─── Patrón del bob 12x8 ─────────────────────────────────────────────────────
constexpr eng::u16 kBobW = 12u;
constexpr eng::u16 kBobH = 8u;

/// Borde color 3, interior damero 1/2, y (5,3) transparente (color 0).
[[nodiscard]] eng::u8 pat(eng::u16 cx, eng::u16 cy) {
	if (cx == 0u || cx + 1u == kBobW || cy == 0u || cy + 1u == kBobH) {
		return 3u;
	}
	if (cx == 5u && cy == 3u) {
		return 0u;
	}
	return ((cx + cy) & 1u) != 0u ? 1u : 2u;
}

[[nodiscard]] bool pat_mask(eng::u16 cx, eng::u16 cy) {
	return cx == 0u || cx + 1u == kBobW || cy == 0u || cy + 1u == kBobH;
}

// ─── Helpers de vistas Chip (escape documentado de los tests host) ───────────
template <class Tag = eng::BobTag, class T, eng::usize N>
eng::ChipView<Tag> chip_view(const T (&a)[N]) {
	return eng::ChipView<Tag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(a),
		static_cast<eng::usize>(N) * sizeof(T)};
}

[[nodiscard]] eng::test::SoftBitmap bitmap() {
	return eng::test::SoftBitmap {g_bitmap, kRowBytes, kHeight, kPlanes, false, 0u};
}

[[nodiscard]] eng::test::SoftBitmap bitmap_il() {
	return eng::test::SoftBitmap {g_bitmap_il, kRowBytes, kHeight, 2u, true, 0u};
}

[[nodiscard]] eng::graphics::BobTarget target() {
	return eng::graphics::make_bob_target(chip_view<eng::PlaneTag>(g_bitmap), kRowBytes, kHeight,
					      kPlanes, eng::graphics::BobLayout::Planar);
}

/// Destino con planos intercalados por fila (bitmap de dual playfield): el BOB se dibuja
/// en un solo pase con `bitplane_count == 1` (filas físicas = `alto × planos`).
[[nodiscard]] eng::graphics::BobTarget target_il() {
	return eng::graphics::make_bob_target(chip_view<eng::PlaneTag>(g_bitmap_il), kRowBytes, kHeight,
					      2u, eng::graphics::BobLayout::Interleaved);
}

void reset_bitmap() {
	std::memset(g_bitmap, 0, sizeof(g_bitmap));
}

[[nodiscard]] eng::u16 row_words_of(eng::u16 w) {
	return static_cast<eng::u16>((w + 15u) / 16u + 1u); // base + guarda
}

[[nodiscard]] eng::u16 words_per_plane(eng::u16 w, eng::u16 h) {
	return static_cast<eng::u16>(row_words_of(w) * h);
}

/// OR de bits en una palabra de la hoja con **orden Amiga (big-endian)**: la memoria del
/// host es little-endian, así que se escribe byte a byte (el ejecutor lee BE, como el
/// Blitter). `bit 15` = píxel izquierdo del grupo de 16.
void be_or(eng::u8* sheet, eng::u32 word_index, eng::u16 bits) {
	eng::u8* const p = sheet + word_index * 2u;
	const eng::u16 cur = static_cast<eng::u16>((static_cast<eng::u16>(p[0]) << 8u) | p[1]);
	const eng::u16 next = static_cast<eng::u16>(cur | bits);
	p[0] = static_cast<eng::u8>(next >> 8u);
	p[1] = static_cast<eng::u8>(next & 0xffu);
}

/// Escribe una fila del patrón en una hoja planar (planos contiguos, guarda por fila).
void write_planar_row(eng::u8* sheet, eng::u16 plane_off_words, eng::u16 row, eng::u16 w,
		      eng::u16 stride_words, eng::u16 y_in_bob, eng::u8 value_mask) {
	for (eng::u16 cx = 0u; cx < w; ++cx) {
		const eng::u8 v = pat(cx, y_in_bob);
		if ((v & value_mask) == 0u) {
			continue;
		}
		const eng::u16 wi = static_cast<eng::u16>(cx / 16u);
		const eng::u16 bit = static_cast<eng::u16>(15u - (cx % 16u));
		be_or(sheet, static_cast<eng::u32>(plane_off_words + row * stride_words + wi),
		      static_cast<eng::u16>(1u << bit));
	}
}

/// Hoja planar 2 planos con guarda por fila.
void build_planar_sheet() {
	std::memset(g_sheet, 0, sizeof(g_sheet));
	const eng::u16 rw = row_words_of(kBobW);
	const eng::u16 wpp = words_per_plane(kBobW, kBobH);
	for (eng::u8 p = 0u; p < 2u; ++p) {
		for (eng::u16 row = 0u; row < kBobH; ++row) {
			write_planar_row(g_sheet, static_cast<eng::u16>(p * wpp), row, kBobW, rw, row,
					 static_cast<eng::u8>(1u << p));
		}
	}
}

/// Máscara planar (1 plano, misma geometría con guarda).
void build_mask_plane() {
	std::memset(g_mask, 0, sizeof(g_mask));
	const eng::u16 rw = row_words_of(kBobW);
	for (eng::u16 row = 0u; row < kBobH; ++row) {
		for (eng::u16 cx = 0u; cx < kBobW; ++cx) {
			if (!pat_mask(cx, row)) {
				continue;
			}
			const eng::u16 wi = static_cast<eng::u16>(cx / 16u);
			const eng::u16 bit = static_cast<eng::u16>(15u - (cx % 16u));
			be_or(g_mask, static_cast<eng::u32>(row * rw + wi),
			      static_cast<eng::u16>(1u << bit));
		}
	}
}

/// Hoja planar **sólida** de color 2 (plano 1) — imagen "plana" para aislar la semántica de
/// la máscara en el cookie-cut (el resultado debe ser imagen dentro y preservado fuera).
void build_solid_sheet2() {
	std::memset(g_sheet, 0, sizeof(g_sheet));
	const eng::u16 rw = row_words_of(kBobW);
	const eng::u16 wpp = words_per_plane(kBobW, kBobH);
	for (eng::u16 row = 0u; row < kBobH; ++row) {
		be_or(g_sheet, static_cast<eng::u32>(wpp + row * rw), 0xfff0u); // plano 1, píxeles 0..11
	}
}

/// Hoja interleaved: por fila lógica, las filas físicas de cada plano (guarda por fila).
void build_interleaved_sheet() {
	std::memset(g_sheet, 0, sizeof(g_sheet));
	const eng::u16 rw = row_words_of(kBobW);
	for (eng::u16 row = 0u; row < kBobH; ++row) {
		for (eng::u8 p = 0u; p < 2u; ++p) {
			const eng::u16 physical = static_cast<eng::u16>(row * 2u + p);
			write_planar_row(g_sheet, 0u, physical, kBobW, rw, row,
					 static_cast<eng::u8>(1u << p));
		}
	}
}

/// Hoja "par" interleaved para cookie-cut de un blit: por fila y plano,
/// [imagen (base+guarda)][máscara (base+guarda)].
void build_pair_sheet() {
	std::memset(g_sheet, 0, sizeof(g_sheet));
	const eng::u16 rw = row_words_of(kBobW);
	const eng::u16 pair = static_cast<eng::u16>(rw * 2u);
	for (eng::u16 row = 0u; row < kBobH; ++row) {
		for (eng::u8 p = 0u; p < 2u; ++p) {
			const eng::u16 base = static_cast<eng::u16>((row * 2u + p) * pair);
			for (eng::u16 cx = 0u; cx < kBobW; ++cx) {
				const eng::u16 wi = static_cast<eng::u16>(cx / 16u);
				const eng::u16 bit = static_cast<eng::u16>(15u - (cx % 16u));
				if ((pat(cx, row) & (1u << p)) != 0u) {
					be_or(g_sheet, static_cast<eng::u32>(base + wi),
					      static_cast<eng::u16>(1u << bit));
				}
				if (pat_mask(cx, row)) {
					be_or(g_sheet, static_cast<eng::u32>(base + rw + wi),
					      static_cast<eng::u16>(1u << bit));
				}
			}
		}
	}
}

/// Hoja planar 4 planos (valores 0..15) con guarda.
void build_planar_sheet4() {
	std::memset(g_sheet, 0, sizeof(g_sheet));
	const eng::u16 rw = row_words_of(kBobW);
	const eng::u16 wpp = words_per_plane(kBobW, kBobH);
	for (eng::u8 p = 0u; p < 4u; ++p) {
		for (eng::u16 row = 0u; row < kBobH; ++row) {
			for (eng::u16 cx = 0u; cx < kBobW; ++cx) {
				const eng::u8 v = static_cast<eng::u8>((cx + 3u * row) & 0x0fu);
				if ((v & (1u << p)) == 0u) {
					continue;
				}
				const eng::u16 wi = static_cast<eng::u16>(cx / 16u);
				const eng::u16 bit = static_cast<eng::u16>(15u - (cx % 16u));
				be_or(g_sheet, static_cast<eng::u32>(p * wpp + row * rw + wi),
				      static_cast<eng::u16>(1u << bit));
			}
		}
	}
}

/// Bob base (hoja planar 2 planos).
[[nodiscard]] eng::graphics::Bob make_bob(eng::graphics::BobDraw draw = eng::graphics::BobDraw::Or) {
	eng::graphics::Bob b {};
	b.sheet = chip_view(g_sheet);
	b.width = kBobW;
	b.height = kBobH;
	b.planes = 2u;
	b.layout = eng::graphics::BobLayout::Planar;
	b.draw = draw;
	return b;
}

/// Comprueba el patrón completo del bob dibujado en (bx, by) sobre el bitmap dado.
void check_bob_at(const eng::test::SoftBitmap& bm, eng::s16 bx, eng::s16 by, const char* what) {
	for (eng::u16 cy = 0u; cy < kBobH; ++cy) {
		for (eng::u16 cx = 0u; cx < kBobW; ++cx) {
			const eng::u8 want = pat(cx, cy);
			const eng::u8 got =
				eng::test::soft_color(bm, static_cast<eng::s16>(bx + cx),
						      static_cast<eng::s16>(by + cy));
			if (got != want) {
				std::printf("  [FAIL] %s: pixel (%u,%u) color %u != %u\n", what,
					    static_cast<unsigned>(bx + cx),
					    static_cast<unsigned>(by + cy),
					    static_cast<unsigned>(got), static_cast<unsigned>(want));
				++g_fail;
				return;
			}
		}
	}
}

void check_clear_rect(eng::s16 x0, eng::s16 y0, eng::u16 w, eng::u16 h, const char* what) {
	for (eng::u16 cy = 0u; cy < h; ++cy) {
		for (eng::u16 cx = 0u; cx < w; ++cx) {
			const eng::u8 got = eng::test::soft_color(bitmap(), static_cast<eng::s16>(x0 + cx),
								  static_cast<eng::s16>(y0 + cy));
			if (got != 0u) {
				std::printf("  [FAIL] %s: (%u,%u) = %u (esperado 0)\n", what,
					    static_cast<unsigned>(x0 + cx), static_cast<unsigned>(y0 + cy),
					    static_cast<unsigned>(got));
				++g_fail;
				return;
			}
		}
	}
}

void expect_result(const eng::test::SoftBlitterResult& res, const char* what) {
	if (res.unsupported) {
		std::printf("  [FAIL] %s: job no soportado por el ejecutor SW\n", what);
		++g_fail;
	}
	if (res.out_of_bounds) {
		std::printf("  [FAIL] %s: escritura fuera de fila/plano (job %u)\n", what,
			    static_cast<unsigned>(res.oob_job));
		++g_fail;
	}
}

void check_canaries(const char* what) {
	for (int i = 0; i < 4; ++i) {
		CHECK(g_canary_lo[i] == (i % 2 == 0 ? 0xdeadu : 0xbeefu));
		CHECK(g_canary_hi[i] == (i % 2 == 0 ? 0xcafeu : 0xbabeu));
	}
	(void)what;
}

} // namespace

int main() {
	std::printf("Test HOST-439 bob_render_matrix (ejecutor SW del FramePlan)\n");
	std::printf("==========================================================\n");

	// Cada caso construye la hoja que necesita (g_sheet es compartida).

	// ── T1: OrBlob alineado ────────────────────────────────────────────────
	{
		build_planar_sheet();
		reset_bitmap();
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, make_bob(), 0u, 16, 4, target()));
		const auto res = eng::test::execute_plan(plan, bitmap());
		expect_result(res, "T1");
		check_bob_at(bitmap(), 16, 4, "T1 OrBlob");
		// Fuera del bob: intacto. Planos 2-3 (no usados por el bob): intactos.
		CHECK(eng::test::soft_color(bitmap(), 15, 4) == 0u);
		CHECK(eng::test::soft_color(bitmap(), 28, 4) == 0u);
		CHECK(eng::test::soft_color(bitmap(), 16, 12) == 0u);
		CHECK(!eng::test::soft_pixel(bitmap(), 20, 6, 2u));
		CHECK(!eng::test::soft_pixel(bitmap(), 20, 6, 3u));
	}

	// ── T2: OrBlob con desplazamiento fino (barrel, cruza palabra) ─────────
	{
		reset_bitmap();
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, make_bob(), 0u, 21, 4, target()));
		const auto res = eng::test::execute_plan(plan, bitmap());
		expect_result(res, "T2");
		check_bob_at(bitmap(), 21, 4, "T2 OrBlob shift 5");
		CHECK(eng::test::soft_color(bitmap(), 20, 4) == 0u); // bits desplazados fuera
		CHECK(eng::test::soft_color(bitmap(), 33, 4) == 0u); // más allá del borde derecho
	}

	// ── T3: Opaque ($F0) sobre relleno previo ──────────────────────────────
	{
		build_planar_sheet();
		reset_bitmap();
		// Pre-rellena el área con color 3 (ambos planos a 1).
		for (eng::u16 y = 4u; y < 12u; ++y) {
			for (eng::u16 x = 16u; x < 28u; ++x) {
				eng::u8* p0 = bitmap().row_ptr(0u, y);
				eng::u8* p1 = bitmap().row_ptr(1u, y);
				p0[x >> 3] |= static_cast<eng::u8>(1u << (7 - (x & 7)));
				p1[x >> 3] |= static_cast<eng::u8>(1u << (7 - (x & 7)));
			}
		}
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, make_bob(eng::graphics::BobDraw::Opaque), 0u, 16, 4,
					      target()));
		const auto res = eng::test::execute_plan(plan, bitmap());
		expect_result(res, "T3");
		check_bob_at(bitmap(), 16, 4, "T3 Opaque");
		// El píxel transparente del patrón (5,3) DEBE quedar a 0 (copia opaca).
		CHECK(eng::test::soft_color(bitmap(), 21, 7) == 0u);
	}

	// ── T4: cookie-cut planar con máscara separada ($CA) ───────────────────
	{
		build_solid_sheet2();
		build_mask_plane();
		reset_bitmap();
		// Pre-rellena el área con color 1 (plano 0) para ver la preservación.
		for (eng::u16 y = 4u; y < 12u; ++y) {
			for (eng::u16 x = 16u; x < 28u; ++x) {
				bitmap().row_ptr(0u, y)[x >> 3] |= static_cast<eng::u8>(1u << (7 - (x & 7)));
			}
		}
		eng::graphics::Bob b = make_bob(eng::graphics::BobDraw::CookieCut);
		b.mask = chip_view(g_mask);
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, b, 0u, 16, 4, target()));
		const auto res = eng::test::execute_plan(plan, bitmap());
		expect_result(res, "T4");
		// Borde (máscara=1): imagen (color 2). Interior (máscara=0): preservado (color 1).
		CHECK(eng::test::soft_color(bitmap(), 16, 4) == 2u);
		CHECK(eng::test::soft_color(bitmap(), 17, 4) == 2u);
		CHECK(eng::test::soft_color(bitmap(), 17, 5) == 1u);
		CHECK(eng::test::soft_color(bitmap(), 16, 11) == 2u); // borde inferior
		CHECK(eng::test::soft_color(bitmap(), 15, 4) == 0u); // fuera: intacto
	}

	// ── T4b: cookie-cut planar + máscara SEPARADA → destino INTERLEAVED ────
	// El caso que destapó DT-012: hoja planar y máscara de `height` filas sobre un destino
	// interleaved. El blit único de `height*planos` filas leería la máscara más allá de su
	// plano; aquí se deja un **canario 0xFFFF tras la máscara** para que esa lectura se vea:
	// con el bug, el plano 1 sale con máscara=1 en TODAS las filas (color 3 en el interior);
	// con el arreglo (por plano, misma máscara por plano) el interior queda preservado.
	{
		build_solid_sheet2();
		build_mask_plane();
		const eng::u16 mask_used = static_cast<eng::u16>(row_words_of(kBobW) * kBobH * 2u);
		std::memset(g_mask + mask_used, 0xff, sizeof(g_mask) - mask_used);
		std::memset(g_bitmap_il, 0, sizeof(g_bitmap_il));
		for (eng::u16 y = 4u; y < 12u; ++y) {
			bitmap_il().row_ptr(0u, y)[2] = 0xffu; // color 1 de fondo, cols 16..31
			bitmap_il().row_ptr(0u, y)[3] = 0xffu;
		}
		eng::graphics::Bob b = make_bob(eng::graphics::BobDraw::CookieCut);
		b.mask = chip_view(g_mask);
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, b, 0u, 16, 4, target_il()));
		const auto res = eng::test::execute_plan(plan, bitmap_il());
		expect_result(res, "T4b");
		// Borde (máscara=1): imagen (color 2, el plano 0 queda a 0 por el minterm $CA).
		CHECK(eng::test::soft_color(bitmap_il(), 16, 4) == 2u);
		CHECK(eng::test::soft_color(bitmap_il(), 27, 11) == 2u);
		// Interior (máscara=0): preservado (color 1) — el canario NO debe filtrarse.
		CHECK(eng::test::soft_color(bitmap_il(), 17, 5) == 1u);
		CHECK(eng::test::soft_color(bitmap_il(), 20, 8) == 1u);
		// Fuera: intacto. (28,4) cae en la palabra procesada pero fuera de la máscara:
		// preservado (C) = color 1 del pre-relleno; (15,4) ni se toca.
		CHECK(eng::test::soft_color(bitmap_il(), 15, 4) == 0u);
		CHECK(eng::test::soft_color(bitmap_il(), 28, 4) == 1u);
		CHECK(eng::test::soft_color(bitmap_il(), 16, 12) == 0u);
	}

	// ── T5: destino y hoja interleaved (un pase, filas físicas = alto×planos) ─
	{
		build_interleaved_sheet();
		std::memset(g_bitmap_il, 0, sizeof(g_bitmap_il));
		eng::graphics::Bob b = make_bob();
		b.layout = eng::graphics::BobLayout::Interleaved;
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, b, 0u, 21, 4, target_il()));
		const auto res = eng::test::execute_plan(plan, bitmap_il());
		expect_result(res, "T5");
		check_bob_at(bitmap_il(), 21, 4, "T5 interleaved");
		CHECK(eng::test::soft_color(bitmap_il(), 20, 4) == 0u);
		CHECK(eng::test::soft_color(bitmap_il(), 33, 4) == 0u);
	}

	// ── T6: cookie-cut interleaved "par" ($CA, un solo blit) ───────────────
	// Bob de 16 px (la mitad de una palabra): imagen+máscara en la misma fila física, sin
	// guarda (contrato `make_interleaved_masked_bob`). El rabo de 11..15 queda transparente
	// en imagen y máscara (es el margen que el barrel toma del par anterior).
	{
		std::memset(g_bitmap_il, 0, sizeof(g_bitmap_il));
		// Pre-rellena el área de la palabra destino (16..31) con color 1 (plano 0).
		for (eng::u16 y = 4u; y < 12u; ++y) {
			bitmap_il().row_ptr(0u, y)[2] = 0xffu;
			bitmap_il().row_ptr(0u, y)[3] = 0xffu;
		}
		std::memset(g_sheet, 0, sizeof(g_sheet));
		for (eng::u16 row = 0u; row < kBobH; ++row) {
			for (eng::u8 p = 0u; p < 2u; ++p) {
				const eng::u16 base = static_cast<eng::u16>((row * 2u + p) * 2u);
				// imagen: píxeles 0..10 = 2 (solo plano 1); máscara: píxeles 0..10 = 1.
				if (p == 1u) {
					be_or(g_sheet, base, 0xffe0u); // bits 15..5 = píxeles 0..10
				}
				be_or(g_sheet, static_cast<eng::u32>(base + 1u), 0xffe0u); // máscara
			}
		}
		eng::graphics::Bob b {};
		b.sheet = chip_view(g_sheet);
		b.width = 16u;
		b.height = kBobH;
		b.planes = 2u;
		b.layout = eng::graphics::BobLayout::Interleaved;
		b.draw = eng::graphics::BobDraw::CookieCut;
		b.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, b, 0u, 21, 4, target_il()));
		const auto res = eng::test::execute_plan(plan, bitmap_il());
		expect_result(res, "T6");
		// Máscara=1 → imagen (color 2) en los píxeles 21..31.
		CHECK(eng::test::soft_color(bitmap_il(), 21, 4) == 2u);
		CHECK(eng::test::soft_color(bitmap_il(), 31, 11) == 2u);
		// Margen izquierdo (16..20): máscara=0 → preservado (color 1).
		CHECK(eng::test::soft_color(bitmap_il(), 16, 4) == 1u);
		CHECK(eng::test::soft_color(bitmap_il(), 20, 4) == 1u);
		// Fuera de la palabra procesada: intacto.
		CHECK(eng::test::soft_color(bitmap_il(), 32, 4) == 0u);
	}

	// ── T6b: arrastre del barrel enmascarado (`ALWM = 0xFFFF << shift`) ─────
	// Hoja par SIN guarda con el bit BAJO de la última palabra a 1 (píxel 15, el rabo que
	// el shift recorta) y desplazamiento 1: sin la ventana fina de `BLTALWM` (AHRM 6: las
	// máscaras se «anded» al dato CRUDO antes del shift; WinUAE-DBG `blitter.cpp:530-531`)
	// el bit bajo de la MÁSCARA de cada fila reaparece en el borde IZQUIERDO (píxel 0) de
	// la fila siguiente y pinta basura (el `hold` del canal A persiste entre filas).
	{
		std::memset(g_bitmap_il, 0, sizeof(g_bitmap_il));
		std::memset(g_sheet, 0, sizeof(g_sheet));
		// Por fila y plano: [imagen][máscara]; bits 15,14 (píxeles 0,1) + bit 0 (píxel 15).
		for (eng::u16 row = 0u; row < kBobH; ++row) {
			for (eng::u8 p = 0u; p < 2u; ++p) {
				const eng::u16 base = static_cast<eng::u16>((row * 2u + p) * 2u);
				if (p == 1u) {
					be_or(g_sheet, base, 0xc001u); // imagen (solo plano 1)
				}
				be_or(g_sheet, static_cast<eng::u32>(base + 1u), 0xc001u); // máscara
			}
		}
		eng::graphics::Bob b {};
		b.sheet = chip_view(g_sheet);
		b.width = 16u;
		b.height = kBobH;
		b.planes = 2u;
		b.layout = eng::graphics::BobLayout::Interleaved;
		b.draw = eng::graphics::BobDraw::CookieCut;
		b.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, b, 0u, 17, 4, target_il())); // shift = 1
		const auto res = eng::test::execute_plan(plan, bitmap_il());
		expect_result(res, "T6b");
		// Positivo: el arte se pinta desplazado 1 (píxel 0 -> 17) en cada fila.
		for (eng::u16 row = 0u; row < kBobH; ++row) {
			CHECK(eng::test::soft_color(bitmap_il(), 17, static_cast<eng::s16>(4u + row)) == 2u);
		}
		// Borde izquierdo (píxel 0 de la palabra, x=16): limpio en TODAS las filas; la fila 0
		// ya lo estaba (hold = 0), las filas 1+ dependen de `ALWM` (regresión).
		for (eng::u16 row = 0u; row < kBobH; ++row) {
			CHECK(eng::test::soft_color(bitmap_il(), 16, static_cast<eng::s16>(4u + row)) == 0u);
		}
		// El rabo recortado (píxel 15 -> x=32) no toca la palabra siguiente.
		CHECK(eng::test::soft_color(bitmap_il(), 32, 4) == 0u);
	}

	// ── T7: erase box (ClearRect) ──────────────────────────────────────────
	{
		build_planar_sheet();
		reset_bitmap();
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, make_bob(), 0u, 16, 4, target()));
		const eng::graphics::Bob b = make_bob();
		CHECK(eng::graphics::bob_erase_box(plan, b, 14u, 10u, 15, 3, target()));
		const auto res = eng::test::execute_plan(plan, bitmap());
		expect_result(res, "T7");
		check_clear_rect(16, 4, 12, 8, "T7 erase");
		// La caja de borrado (15,3,14x10) toca los píxeles del bob; fuera, intacto.
		CHECK(eng::test::soft_color(bitmap(), 14, 3) == 0u);
		CHECK(eng::test::soft_color(bitmap(), 30, 3) == 0u);
	}

	// ── T8: save-under roundtrip (CopyRect + RestoreRect) ──────────────────
	{
		reset_bitmap();
		// Fondo reconocible.
		for (eng::u16 y = 0u; y < kHeight; ++y) {
			for (eng::u16 x = 0u; x < 128u; ++x) {
				const eng::u8 v = static_cast<eng::u8>((x + y) & 3u);
				for (eng::u8 p = 0u; p < 2u; ++p) {
					if ((v & (1u << p)) != 0u) {
						bitmap().row_ptr(p, y)[x >> 3] |=
							static_cast<eng::u8>(1u << (7 - (x & 7)));
					}
				}
			}
		}
		eng::u8 before[kPlaneBytes * kPlanes];
		std::memcpy(before, g_bitmap, sizeof(before));
		const eng::graphics::Bob b = make_bob();
		const eng::u16 save_words = 2u; // 12 px + shift -> 1..2 palabras
		eng::graphics::FramePlan plan {};
		// En host no hay Chip RAM: escape documentado del test (la vista solo transporta la
		// procedencia; el buffer es el global g_save).
		const eng::ChipView<eng::BobTag> save =
			eng::as_chip(eng::Bytes<eng::BobTag> {g_save, sizeof(g_save)});
		CHECK(eng::graphics::bob_save_box(plan, b, 12u, 8u, 16, 4, target(), save,
						  save_words, 8u));
		CHECK(eng::graphics::bob_draw(plan, make_bob(eng::graphics::BobDraw::Opaque), 0u, 16, 4,
					      target()));
		CHECK(eng::graphics::bob_restore_box(plan, b, 12u, 8u, 16, 4, target(), save,
						     save_words, 8u));
		const auto res = eng::test::execute_plan(plan, bitmap());
		expect_result(res, "T8");
		CHECK(std::memcmp(before, g_bitmap, sizeof(before)) == 0);
	}

	// ── T9: FastCopy con padding (copia opaca tapa la posición previa) ─────
	{
		// Hoja planar con contenido 8x6 y padding 4 (hoja 16x14).
		std::memset(g_sheet, 0, sizeof(g_sheet));
		const eng::u16 pw = 16u;
		const eng::u16 ph = 14u;
		const eng::u16 rw = row_words_of(pw);
		const eng::u16 wpp = static_cast<eng::u16>(rw * ph);
		for (eng::u8 p = 0u; p < 2u; ++p) {
			for (eng::u16 y = 4u; y < 10u; ++y) {
				for (eng::u16 x = 4u; x < 12u; ++x) {
					const eng::u16 wi = static_cast<eng::u16>(x / 16u);
					const eng::u16 bit = static_cast<eng::u16>(15u - (x % 16u));
					be_or(g_sheet, static_cast<eng::u32>(p * wpp + y * rw + wi),
					      static_cast<eng::u16>(1u << bit));
				}
			}
		}
		eng::graphics::Bob b {};
		b.sheet = chip_view(g_sheet);
		b.width = pw;
		b.height = ph;
		b.planes = 2u;
		b.layout = eng::graphics::BobLayout::Planar;
		b.draw = eng::graphics::BobDraw::Opaque;

		reset_bitmap();
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, b, 0u, 20, 10, target()));
		CHECK(eng::graphics::bob_draw(plan, b, 0u, 24, 10, target())); // paso 4 = padding
		const auto res = eng::test::execute_plan(plan, bitmap());
		expect_result(res, "T9");
		// Contenido de la segunda copia (24+4=28..35, 10+4=14..19) en color 3.
		CHECK(eng::test::soft_color(bitmap(), 28, 14) == 3u);
		CHECK(eng::test::soft_color(bitmap(), 35, 19) == 3u);
		// La huella de la primera copia (28..35, 14..19 en su origen) queda TAPADA por el
		// padding de la segunda (que empieza en 20): p. ej. (24,14) era contenido y ahora es 0.
		CHECK(eng::test::soft_color(bitmap(), 24, 14) == 0u);
		CHECK(eng::test::soft_color(bitmap(), 28, 10) == 0u);
	}

	// ── T10: x negativo: NO se dibuja (recorte izquierdo no soportado por el barrel) ──
	{
		build_planar_sheet();
		reset_bitmap();
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, make_bob(), 0u, -4, 4, target()));
		CHECK(plan.blit_job_count() == 0u); // contrato: sin trabajo (no "wrap" a x=12)
		const auto res = eng::test::execute_plan(plan, bitmap());
		expect_result(res, "T10");
		for (eng::u16 y = 4u; y < 12u; ++y) {
			for (eng::u16 x = 0u; x < 20u; ++x) {
				if (eng::test::soft_color(bitmap(), static_cast<eng::s16>(x),
							  static_cast<eng::s16>(y)) != 0u) {
					std::printf("  [FAIL] T10 x<0: pixel (%u,%u) escrito\n",
						    static_cast<unsigned>(x), static_cast<unsigned>(y));
					++g_fail;
					y = 12u;
					break;
				}
			}
		}
		// En el borde (x=0) sí se dibuja normalmente.
		eng::graphics::FramePlan plan2 {};
		CHECK(eng::graphics::bob_draw(plan2, make_bob(), 0u, 0, 4, target()));
		const auto res2 = eng::test::execute_plan(plan2, bitmap());
		expect_result(res2, "T10b");
		check_bob_at(bitmap(), 0, 4, "T10 x=0");
	}

	// ── T11: bob de 4 planos (valores 0..15) ───────────────────────────────
	{
		build_planar_sheet4();
		reset_bitmap();
		eng::graphics::Bob b {};
		b.sheet = chip_view(g_sheet);
		b.width = kBobW;
		b.height = kBobH;
		b.planes = 4u;
		b.layout = eng::graphics::BobLayout::Planar;
		b.draw = eng::graphics::BobDraw::Or;
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, b, 0u, 40, 4, target()));
		const auto res = eng::test::execute_plan(plan, bitmap());
		expect_result(res, "T11");
		for (eng::u16 cy = 0u; cy < kBobH; ++cy) {
			for (eng::u16 cx = 0u; cx < kBobW; ++cx) {
				const eng::u8 want = static_cast<eng::u8>((cx + 3u * cy) & 0x0fu);
				const eng::u8 got = eng::test::soft_color(bitmap(), 40 + cx, 4 + cy);
				if (got != want) {
					std::printf("  [FAIL] T11 4 planos: (%u,%u) = %u != %u\n",
						    static_cast<unsigned>(40 + cx),
						    static_cast<unsigned>(4 + cy),
						    static_cast<unsigned>(got),
						    static_cast<unsigned>(want));
					++g_fail;
					break;
				}
			}
		}
	}

	// ── T12: And ($C0) sobre relleno a 1 (recorta el patrón) ───────────────
	{
		build_planar_sheet();
		reset_bitmap();
		for (eng::u16 y = 4u; y < 12u; ++y) {
			std::memset(bitmap().row_ptr(0u, y) + 2u, 0xff, 2u); // cols 16..31
			std::memset(bitmap().row_ptr(1u, y) + 2u, 0xff, 2u);
		}
		eng::graphics::FramePlan plan {};
		CHECK(eng::graphics::bob_draw(plan, make_bob(eng::graphics::BobDraw::And), 0u, 16, 4,
					      target()));
		const auto res = eng::test::execute_plan(plan, bitmap());
		expect_result(res, "T12");
		check_bob_at(bitmap(), 16, 4, "T12 And");
	}

	// ── T13: fuera de la geometría/reserva del destino: NO se emite blit ────────────────────
	// El Blitter no recorta: un bob a caballo del borde derecho o inferior escribiría en la
	// reserva contigua (contaminación entre buffers). La validación es en el encolado.
	{
		build_planar_sheet();
		reset_bitmap();
		eng::graphics::FramePlan plan {};
		CHECK(!eng::graphics::bob_draw(plan, make_bob(), 0u, 120, 4, target())); // 120+12 > 128
		CHECK(plan.blit_job_count() == 0u);
		eng::graphics::FramePlan plan2 {};
		CHECK(!eng::graphics::bob_draw(plan2, make_bob(), 0u, 40, 58, target())); // 58+8 > 64
		CHECK(plan2.blit_job_count() == 0u);
		eng::graphics::FramePlan plan3 {};
		CHECK(eng::graphics::bob_erase_box(plan3, make_bob(), kBobW, kBobH, 120, 4, target()));
		CHECK(plan3.blit_job_count() == 0u);
		// Reserva MÁS CORTA que la geometría (faltan 16 B del último plano): rechazado.
		eng::graphics::BobTarget short_t = eng::graphics::make_bob_target(
			chip_view<eng::PlaneTag>(g_bitmap).first(kPlaneBytes * kPlanes - 16u),
			kRowBytes, kHeight, kPlanes, eng::graphics::BobLayout::Planar);
		eng::graphics::FramePlan plan4 {};
		CHECK(!eng::graphics::bob_draw(plan4, make_bob(), 0u, 0, 0, short_t));
		CHECK(plan4.blit_job_count() == 0u);
		// En el límite exacto (x=112, 12 px → 124; sin shift, 1 palabra) sí se dibuja.
		eng::graphics::FramePlan plan5 {};
		CHECK(eng::graphics::bob_draw(plan5, make_bob(), 0u, 112, 4, target()));
		const auto res5 = eng::test::execute_plan(plan5, bitmap());
		expect_result(res5, "T13 limite derecho");
		// Con shift (x=116) el barrel procesa una palabra de más: 112+32 > 128 → rechazado.
		eng::graphics::FramePlan plan6 {};
		CHECK(!eng::graphics::bob_draw(plan6, make_bob(), 0u, 116, 4, target()));
		CHECK(plan6.blit_job_count() == 0u);
		check_canaries("T13");
	}

	check_canaries("final");

	if (g_fail == 0) {
		std::printf("OK: matriz de render de bobs validada (14 casos + canarios).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_fail);
	return 1;
}
