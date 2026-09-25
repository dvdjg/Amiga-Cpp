#pragma once

/// \file glyph_cache.hpp
/// **Texto por Blitter con caché de glifos** (`eng`) para `eng::ui`. En vez de pintar el texto
/// píxel a píxel por CPU (`Surface::draw_glyph_row`), precomputa por *code point* la **máscara
/// planar de 1 bit** del glifo (8 filas) y la usa como canal A de un **cookie-cut** del Blitter
/// (`MaskedBobCookieCut`, minterm `$CA`: `D = (A & B) | (~A & D)`). Como fuente B se usa un plano
/// **sólido** (`$FFFF` o `$0000` según el bit de color): el Blitter escribe el color solo dentro
/// del glifo y conserva el fondo fuera. Ver `docs/engine/architecture/GUI_LIBRARY.md` §6.
///
/// Diseño: sin heap, capacidad fija (`GlyphCache<Max>`), reutiliza `Font8` (no hay fuente nueva) y
/// el contrato de `field::FramePlan`/`Rasterizer` (CPU o Blitter). La ruta CPU (referencia de
/// equivalencia) es `Surface::draw_text`; este header es la ruta acelerada. Mismo resultado.
///
/// El Blitter escribe a nivel de **palabra** (16 px), así que el destino de cada blit va alineado;
/// con `x` no alineado el par de glifos se **pre-desplaza** a dos palabras y se emite desde
/// `x & ~15`. Los buffers de máscara y sólido deben vivir en el llamador (el `FramePlan` guarda
/// punteros hasta que se ejecuta).

#include <eng/core/types/box.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/data/utf8.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::graphics {

/// Máscara planar de un glifo `Font8` (8 filas × 1 palabra = 1 bit por píxel en MSB).
/// Se usa como canal A del cookie-cut.
struct GlyphMask {
	eng::u16 rows[eng::Font8::kRows] {}; ///< fila `r`: bit `15-k` = píxel columna `k` (0=izq)

	/// Construye la máscara del code point `cp` con `Font8` (bit `k` de la fila → columna `k`).
	static GlyphMask from_codepoint(eng::u32 cp) noexcept {
		GlyphMask m {};
		const eng::u16 ch = static_cast<eng::u16>(cp);
		for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) {
			const eng::u8 bits = eng::Font8::row(ch, r); // bit k = columna k (0=izq)
			eng::u16 w = 0u;
			for (eng::u8 k = 0u; k < 8u; ++k) {
				if (((bits >> k) & 1u) != 0u) {
					w = static_cast<eng::u16>(w | (0x8000u >> k));
				}
			}
			m.rows[r] = w;
		}
		return m;
	}
};

/// Caché de glifos de capacidad fija: guarda la máscara por *code point* (búsqueda lineal; las
/// UIs usan pocas decenas de glifos distintos). Sin heap.
template <eng::u16 Max = 128u>
class GlyphCache {
public:
	static constexpr eng::u16 kCapacity = Max;

	/// Máscara del code point `cp`, construyéndola y cacheándola si no estaba. Si la caché está
	/// llena, la construye sin cachear (devuelve referencia a un scratch interno).
	[[nodiscard]] const GlyphMask& mask(eng::u32 cp) noexcept {
		for (eng::u16 i = 0u; i < m_count; ++i) {
			if (m_entries[i].cp == cp) {
				return m_entries[i].mask;
			}
		}
		if (m_count < Max) {
			Entry& e = m_entries[m_count];
			e.cp = cp;
			e.mask = GlyphMask::from_codepoint(cp);
			++m_count;
			return e.mask;
		}
		m_scratch = GlyphMask::from_codepoint(cp);
		return m_scratch;
	}

	[[nodiscard]] eng::u16 count() const noexcept { return m_count; }
	void clear() noexcept { m_count = 0u; }

private:
	struct Entry {
		eng::u32 cp = 0u;
		GlyphMask mask {};
	};
	Entry m_entries[Max] {};
	GlyphMask m_scratch {};
	eng::u16 m_count = 0u;
};

/// Dibuja `text` (UTF-8) en `(x, y)` con `color` usando **cookie-cut del Blitter**. El Blitter
/// opera a nivel de **palabra** (16 px), así que el texto se agrupa de **dos glifos en dos glifos**
/// (cada par = una palabra): la máscara del par es la de dos glifos consecutivos (8+8 px). Los
/// glifos se leen con `Font8` y se cachean (`GlyphCache`).
///
/// `x` puede ser arbitrario: alineado a 16 px agrupa **dos glifos por palabra** (2 glifos/blit); con
/// `x` no alineado pre-desplaza el par a **dos palabras** y emite desde `x & ~15` (coste doble solo
/// en esa ruta).
///
/// **`src_scratch` y `mask_scratch` viven en el llamador** (sin heap, **Chip RAM**: el Blitter lee
/// A/B por DMA) y deben **persistir hasta ejecutar el `FramePlan`**: `blit_masked` solo encola el
/// job con punteros a esos buffers, y el plan se ejecuta *después* de que esta función retorne.
/// `src_scratch` necesita `planes * 2 * Font8::kRows` palabras (el **sólido** es igual para todos
/// los pares: se comparte). `mask_scratch` necesita `pares * 2 * Font8::kRows` palabras, una máscara
/// **por par**: reutilizar una sola perdería todas menos la última (el plan se ejecuta al final).
/// `clip`: si no está vacío, solo se emiten los bloques que caben **enteros** (no parte glifos a
/// medias; coherente con `draw_text_clipped`). Devuelve `false` si no se pudo encolar (tamaño de los
/// buffers, planos…). La ruta CPU equivalente es `Surface::draw_text`.
template <eng::u16 Max>
bool draw_text_blit(eng::field::Surface& s, eng::graphics::FramePlan& plan, GlyphCache<Max>& cache,
		    eng::s32 x, eng::s32 y, const char* text, eng::u8 color,
		    eng::Span<eng::u16> src_scratch, eng::Span<eng::u16> mask_scratch, eng::u8 planes,
		    eng::Box clip = {}) noexcept {
	if (text == nullptr || planes == 0u) {
		// Nada que pintar; el clip/alineación se comprueban al emitir.
		return text != nullptr;
	}
	// El Blitter opera a nivel de **palabra** (16 px) y el destino debe estar alineado. Con `x`
	// alineado, cada par de glifos ocupa una palabra (2 glifos/palabra). Con `x` **no** alineado
	// (`sh = x & 15 != 0`) el par desplazado ocupa **dos** palabras: se pre-desplaza la máscara y el
	// sólido en `sh` bits y se emite desde `ax = x & ~15` con `source_shift = 0` (la ruta CPU y la
	// Blitter ven el mismo caso base alineado; no se depende del barrel shifter del Blitter). El
	// coste es el doble de palabras y de ancho de blit solo en esa ruta.
	const eng::s32 sh = x & 15;
	const eng::s32 ax = x & ~15;
	const eng::u16 words_per_pair = (sh != 0) ? 2u : 1u;
	const eng::u32 pair_words = static_cast<eng::u32>(words_per_pair) * eng::Font8::kRows;
	if (src_scratch.size() < static_cast<eng::u32>(planes) * pair_words ||
	    mask_scratch.size() < pair_words) {
		return false;
	}
	const eng::u16 solid_hi = static_cast<eng::u16>(0xffffu >> sh); // sh==0 -> 0xffff
	const eng::u16 solid_lo = static_cast<eng::u16>(0xffffu << (16 - sh));
	// El **sólido** es igual para todos los pares (mismo color): `src_scratch` se comparte (se
	// reescribe con el mismo contenido; los jobs leen su valor final, idéntico). La **máscara**,
	// en cambio, cambia por par y el plan se ejecuta *después*: cada par debe tener su propia
	// máscara persistente en `mask_scratch` (una tras otra, `pair_words` palabras por par).
	// Decodifica el texto a code points, en pares (una palabra = 2 glifos).
	const eng::u8* q = reinterpret_cast<const eng::u8*>(text);
	eng::s32 cx = (sh == 0) ? x : ax; // posición de la palabra física emitida
	eng::u32 pair_index = 0u;
	for (;;) {
		const eng::u32 cp0 = eng::utf8::decode(q);
		if (cp0 == 0u) {
			return true;
		}
		const eng::u32 cp1 = eng::utf8::decode(q); // 0 si fin de texto
		// IMPORTANTE: `cache.mask()` puede **reubicar** las entradas (crecimiento), así que las
		// referencias no se retienen entre llamadas: se copian las filas inmediatamente.
		eng::u16 m0_rows[eng::Font8::kRows];
		if (cp0 >= 32u) {
			const GlyphMask& m0 = cache.mask(cp0);
			for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) m0_rows[r] = m0.rows[r];
		} else {
			for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) m0_rows[r] = 0u;
		}
		eng::u16 m1_rows[eng::Font8::kRows];
		if (cp1 >= 32u) {
			const GlyphMask& m1 = cache.mask(cp1);
			for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) m1_rows[r] = m1.rows[r];
		} else {
			for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) m1_rows[r] = 0u;
		}
		// Máscara **propia de este par** en `mask_scratch` (persiste hasta ejecutar el plan). Glifo 0
		// en la mitad alta, glifo 1 en la baja (MSB = izquierda). Con `sh != 0` se pre-desplaza a 2
		// palabras intercaladas por fila (el par de 16 px ocupa [sh, sh+16)).
		if (mask_scratch.size() < static_cast<eng::u32>(pair_index + 1u) * pair_words) {
			return false; // el llamador debe dimensionar `mask_scratch` para todos los pares
		}
		eng::u16* mask_par = mask_scratch.data() + pair_index * pair_words;
		++pair_index;
		for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) {
			const eng::u16 pair = static_cast<eng::u16>(m0_rows[r] | (m1_rows[r] >> 8));
			if (sh == 0) {
				mask_par[r] = pair;
			} else {
				// Bit 15 = columna 0 (izquierda): mover el par `sh` px a la derecha es `>> sh`.
				mask_par[static_cast<eng::usize>(r) * 2u] = static_cast<eng::u16>(pair >> sh);
				mask_par[static_cast<eng::usize>(r) * 2u + 1u] =
					static_cast<eng::u16>(pair << (16 - sh));
			}
		}
		// Sólido por plano en `src_scratch` (layout intercalado por fila, `words_per_pair` palabras).
		for (eng::u8 p = 0u; p < planes; ++p) {
			const eng::u16 v = ((color >> p) & 1u) != 0u ? 0xffffu : 0x0000u;
			eng::u16* base = src_scratch.data() + static_cast<eng::u32>(p) * pair_words;
			for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) {
				if (sh == 0) {
					base[r] = v;
				} else {
					base[static_cast<eng::usize>(r) * 2u] = static_cast<eng::u16>(v & solid_hi);
					base[static_cast<eng::usize>(r) * 2u + 1u] = static_cast<eng::u16>(v & solid_lo);
				}
			}
		}
		// Recorte: si hay `clip`, solo se emite el bloque si **cabe entero** (no se parte un glifo a
		// medias, como `draw_text_clipped`). `blit_masked` rechazaría si excede el clip.
		bool emit = true;
		if (!clip.empty()) {
			const eng::s32 right = static_cast<eng::s32>(clip.x) + clip.w;
			emit = cx >= clip.x &&
			       (cx + static_cast<eng::s32>(words_per_pair) * 16) <= right &&
			       y >= clip.y && static_cast<eng::s32>(y) + 8 <= static_cast<eng::s32>(clip.y) + clip.h;
		}
		if (emit) {
			// Un solo `blit_masked` con TODOS los planos: el destino recorre sus planos contiguos
			// (mientras que varios blits de 1 plano escribirían siempre el plano 0).
			const eng::u16 row_bytes = static_cast<eng::u16>(words_per_pair * 2u);
			const eng::u32 plane_stride = pair_words * 2u;
			const bool ok = s.blit_masked(plan,
						      eng::Span<const eng::u16>(src_scratch.data(),
										static_cast<eng::u32>(planes) * pair_words),
						      eng::Span<const eng::u16>(mask_par, pair_words),
						      cx, y, static_cast<eng::u16>(words_per_pair * 16u), 8u,
						      row_bytes, plane_stride, planes);
			if (!ok) {
				return false;
			}
		}
		if (cp1 == 0u) {
			return true;
		}
		cx = static_cast<eng::s32>(cx + 16);
	}
}

/// Texto con **sombra** (dos pasadas por Blitter): el texto en `shadow` desplazado **1 px abajo**
/// (y+1) y luego el texto en `color` (y). La pasada de sombra pinta primero; el texto encima la
/// tapa dentro del glifo, dejando la sombra como borde inferior (efecto de relieve típico de UI).
/// Mismo contrato que `draw_text_blit` (`x` arbitrario, buffers del llamador en **Chip RAM** que
/// persisten hasta ejecutar el plan, `planes`, `clip`). **Ojo**: son **dos pasadas** (sombra y
/// texto) que comparten `mask_scratch`; como el plan se ejecuta al final, la segunda pasada
/// pisaría las máscaras de la primera, así que `mask_scratch` debe tener el **doble**
/// (`2 * pares * 2 * Font8::kRows`): la primera mitad para la sombra y la segunda para el texto.
template <eng::u16 Max>
bool draw_text_shadow_blit(eng::field::Surface& s, eng::graphics::FramePlan& plan,
			   GlyphCache<Max>& cache, eng::s32 x, eng::s32 y, const char* text,
			   eng::u8 color, eng::u8 shadow, eng::Span<eng::u16> src_scratch,
			   eng::Span<eng::u16> mask_scratch, eng::u8 planes, eng::Box clip = {}) noexcept {
	// La sombra se dibuja en (y+1) con `shadow`; el texto en (y) con `color`.
	if (!draw_text_blit(s, plan, cache, x, static_cast<eng::s32>(y + 1), text, shadow, src_scratch,
			    mask_scratch, planes, clip)) {
		return false;
	}
	return draw_text_blit(s, plan, cache, x, y, text, color, src_scratch, mask_scratch, planes,
			      clip);
}

} // namespace eng::graphics
