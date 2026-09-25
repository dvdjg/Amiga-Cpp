// ============================================================================
// Test HOST-313: GUI — texto por Blitter con cache de glifos (equivalencia CPU).
// ============================================================================
//
// Valida `eng/graphics/glyph_cache.hpp`: la mascara planar de un glifo (`GlyphMask`) y la ruta
// `draw_text_blit` (cookie-cut del Blitter, 2 glifos por palabra). En host el rasterizador es CPU,
// asi que se comprueba la **equivalencia pixel a pixel** con la ruta CPU `Surface::draw_text`
// (la referencia canonica del protocolo). En hardware el mismo seam encola `MaskedBobCookieCut`.
// Ver PROTOCOLO_ETAPAS_GRAFICOS.md (etapa 4) y GUI_LIBRARY.md 6.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/313_ui_text_blit

#include <cstdio>

#include <eng/core/types/box.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/glyph_cache.hpp>
#include <eng/ui/theme.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

constexpr eng::u16 kSW = 64u;
constexpr eng::u16 kSH = 32u;
constexpr eng::u16 kRow = static_cast<eng::u16>(((kSW / 8u) + 3u) & ~3u); // 8
constexpr eng::u32 kPlaneStride = static_cast<eng::u32>(kRow) * kSH;     // 256
constexpr eng::u8 kPlanes = 4u;

eng::u8 pixel_at(const eng::u8* base, eng::s16 x, eng::s16 y) {
	eng::u8 c = 0u;
	for (eng::u8 p = 0u; p < kPlanes; ++p) {
		const eng::u16* w = reinterpret_cast<const eng::u16*>(
			base + p * kPlaneStride + static_cast<eng::u32>(y) * kRow +
			static_cast<eng::u32>(x / 16) * 2u);
		const eng::u8 bit = static_cast<eng::u8>((*w >> (15u - (x & 15))) & 1u);
		c = static_cast<eng::u8>(c | static_cast<eng::u8>(bit << p));
	}
	return c;
}

} // namespace

int main() {
	// --- Mascara del glifo 'A' ---
	const eng::graphics::GlyphMask m = eng::graphics::GlyphMask::from_codepoint('A');
	check(m.rows[0] != 0u, "mascara de 'A' no vacia (fila 0)");
	// Fila 0 de 'A' (0x38) = bits 3,4,5 -> columnas 3,4,5 -> MSB: 0x8000>>3..5.
	{
		const eng::u8 bits = eng::Font8::row('A', 0);
		eng::u16 want = 0u;
		for (eng::u8 k = 0u; k < 8u; ++k) {
			if (((bits >> k) & 1u) != 0u) want = static_cast<eng::u16>(want | (0x8000u >> k));
		}
		check(m.rows[0] == want, "mascara de 'A' fila 0 == Font8 (MSB izq)");
	}

	// --- Cache ---
	eng::graphics::GlyphCache<8> cache;
	const auto& ma = cache.mask('A');
	const auto& mb = cache.mask('B');
	check(cache.count() == 2u, "cache contabiliza 2 glifos");
	check(cache.mask('A').rows[0] == ma.rows[0], "cache devuelve la misma mascara");
	check(ma.rows[0] != mb.rows[0], "'A' y 'B' tienen mascaras distintas");

	// --- Equivalencia CPU vs draw_text_blit ---
	alignas(2) eng::u8 mem_ref[kPlaneStride * kPlanes] {};
	alignas(2) eng::u8 mem_blit[kPlaneStride * kPlanes] {};
	alignas(2) eng::u16 scratch[kPlanes * eng::Font8::kRows] {};

	eng::field::ContiguousPlayfield pf_ref {};
	eng::field::ContiguousPlayfield pf_blit {};
	check(pf_ref.bind_raw(mem_ref, sizeof(mem_ref), kSW, kSH, kPlanes), "bind pf_ref");
	check(pf_blit.bind_raw(mem_blit, sizeof(mem_blit), kSW, kSH, kPlanes), "bind pf_blit");
	eng::field::Surface s_ref {pf_ref, eng::field::SurfaceRect {0, 0, kSW, kSH}};
	eng::field::Surface s_blit {pf_blit, eng::field::SurfaceRect {0, 0, kSW, kSH}};

	// Fondo distinto de 0 para detectar que el cookie-cut NO borra fuera del glifo.
	constexpr eng::u8 kBg = 2u;
	s_ref.fill_rect(0, 0, kSW, kSH, kBg);
	s_blit.fill_rect(0, 0, kSW, kSH, kBg);

	// "AB" en x=0 (alineado), color 3.
	const char* txt = "AB";
	check(s_ref.draw_text(0, 4, txt, 3u), "draw_text CPU");
	eng::graphics::FramePlan plan {};
	check(eng::graphics::draw_text_blit(s_blit, plan, cache, 0, 4, txt, 3u,
					    eng::Span<eng::u16>(scratch), kPlanes),
	      "draw_text_blit encola");

	bool same = true;
	int diff = 0;
	for (eng::u32 i = 0u; i < kPlaneStride * kPlanes; ++i) {
		if (mem_ref[i] != mem_blit[i]) {
			same = false;
			++diff;
		}
	}
	check(same, "draw_text_blit == draw_text (pixel a pixel, 2 glifos/palabra)");

	// Texto de longitud impar ("A"): la segunda mitad de la palabra no debe pintarse.
	alignas(2) eng::u8 m2_ref[kPlaneStride * kPlanes] {};
	alignas(2) eng::u8 m2_blit[kPlaneStride * kPlanes] {};
	eng::field::ContiguousPlayfield p2r {}, p2b {};
	p2r.bind_raw(m2_ref, sizeof(m2_ref), kSW, kSH, kPlanes);
	p2b.bind_raw(m2_blit, sizeof(m2_blit), kSW, kSH, kPlanes);
	eng::field::Surface s2r {p2r, eng::field::SurfaceRect {0, 0, kSW, kSH}};
	eng::field::Surface s2b {p2b, eng::field::SurfaceRect {0, 0, kSW, kSH}};
	s2r.fill_rect(0, 0, kSW, kSH, kBg);
	s2b.fill_rect(0, 0, kSW, kSH, kBg);
	s2r.draw_text(0, 4, "A", 3u);
	eng::graphics::FramePlan plan2 {};
	eng::graphics::draw_text_blit(s2b, plan2, cache, 0, 4, "A", 3u,
				      eng::Span<eng::u16>(scratch), kPlanes);
	bool same2 = true;
	for (eng::u32 i = 0u; i < kPlaneStride * kPlanes; ++i) {
		if (m2_ref[i] != m2_blit[i]) same2 = false;
	}
	check(same2, "draw_text_blit == draw_text (longitud impar)");
	// La segunda mitad (x=8) debe quedar con el fondo.
	check(pixel_at(m2_blit, 12, 5) == kBg, "mitad derecha conserva el fondo");

	// x no alineado -> rechazo.
	eng::graphics::FramePlan plan3 {};
	check(!eng::graphics::draw_text_blit(s_blit, plan3, cache, 4, 4, "A", 3u,
					     eng::Span<eng::u16>(scratch), kPlanes),
	      "x no alineado a palabra -> false");

	// --- Clip por palabra: con un clip que solo contiene el primer par, el segundo no se pinta ---
	{
		alignas(2) eng::u8 mclip[kPlaneStride * kPlanes] {};
		eng::field::ContiguousPlayfield pfc {};
		pfc.bind_raw(mclip, sizeof(mclip), kSW, kSH, kPlanes);
		eng::field::Surface sc {pfc, eng::field::SurfaceRect {0, 0, kSW, kSH}};
		sc.fill_rect(0, 0, kSW, kSH, kBg);
		eng::graphics::GlyphCache<8> cc;
		eng::u16 sc2[kPlanes * eng::Font8::kRows] {};
		eng::graphics::FramePlan pc {};
		// "ABCD" = 2 palabras; clip x=0..16 -> solo la primera palabra (A,B) cabe.
		(void)eng::graphics::draw_text_blit(sc, pc, cc, 0, 4, "ABCD", 3u,
						    eng::Span<eng::u16>(sc2), kPlanes,
						    eng::ui::Rect {0, 0, 16, kSH});
		check(pixel_at(mclip, 2, 5) == 3u, "clip: primer par pintado (A)");
		check(pixel_at(mclip, 20, 5) == kBg, "clip: segundo par fuera del clip (no pintado)");
	}

	// --- Sombra: color sombra 1 px abajo, texto encima ---
	{
		alignas(2) eng::u8 msh[kPlaneStride * kPlanes] {};
		eng::field::ContiguousPlayfield pfsh {};
		pfsh.bind_raw(msh, sizeof(msh), kSW, kSH, kPlanes);
		eng::field::Surface ssh {pfsh, eng::field::SurfaceRect {0, 0, kSW, kSH}};
		ssh.fill_rect(0, 0, kSW, kSH, 0u); // fondo 0 para distinguir la sombra (2)
		eng::graphics::GlyphCache<8> cs;
		eng::u16 sc3[kPlanes * eng::Font8::kRows] {};
		eng::graphics::FramePlan ps {};
		(void)eng::graphics::draw_text_shadow_blit(ssh, ps, cs, 0, 4, "A", 3u, 2u,
							   eng::Span<eng::u16>(sc3), kPlanes);
		// La sombra (color 2) aparece 1 px por debajo del texto (color 3).
		bool shadow_seen = false, text_seen = false;
		for (eng::s16 y = 4; y < 14; ++y) {
			for (eng::s16 x = 0; x < 8; ++x) {
				const eng::u8 c = pixel_at(msh, x, y);
				if (c == 2u) shadow_seen = true;
				if (c == 3u) text_seen = true;
			}
		}
		check(shadow_seen && text_seen, "sombra: color de sombra (2) y de texto (3) presentes");
	}

	// El glifo pintado realmente tiene tinta del color 3 dentro de 'A'.
	check(pixel_at(mem_blit, 2, 5) == 3u || pixel_at(mem_blit, 3, 5) == 3u ||
		      pixel_at(mem_blit, 4, 5) == 3u,
	      "hay tinta (color 3) en la zona del glifo");

	if (failures == 0) {
		std::printf("OK: texto por Blitter (cache de glifos) == CPU validado.\n");
	} else {
		std::printf("FALLOS: %d\n", failures);
	}
	return failures == 0 ? 0 : 1;
}
