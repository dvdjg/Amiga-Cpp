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
/// Restricción del Blitter: el destino debe estar **alineado a palabra** (16 px). El texto se
/// dibuja en x múltiplo de 16; `draw_text_blit` lo exige y avisa (`false`) si no.

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

/// Pinta el plan sólido del bit `p` del color: `$FFFF` en todas las filas si `color` tiene el bit
/// `p`, `$0000` si no. Es la fuente B del cookie-cut (el Blitter copia este patrón dentro del
/// glifo). `rows` debe tener `Font8::kRows` palabras.
inline void solid_color_plane(eng::u16* rows, eng::u8 color, eng::u8 p) noexcept {
	const eng::u16 v = ((color >> p) & 1u) != 0u ? 0xffffu : 0x0000u;
	for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) {
		rows[r] = v;
	}
}

/// Dibuja `text` (UTF-8) en `(x, y)` con `color` usando **cookie-cut del Blitter**. El Blitter
/// opera a nivel de **palabra** (16 px), así que el texto se agrupa de **dos glifos en dos glifos**
/// (cada par = una palabra): la máscara del par es la de dos glifos consecutivos (8+8 px). Los
/// glifos se leen con `Font8` y se cachean (`GlyphCache`).
///
/// `x` debe ser múltiplo de 16 (restricción del Blitter). `src_scratch` es un buffer de trabajo
/// (`planes * Font8::kRows` palabras) que construye el plan sólido del color; vive en el llamador
/// (sin heap). Devuelve `false` si no se pudo encolar (x no alineado, clip, tamaño…). La ruta CPU
/// equivalente es `Surface::draw_text` (mismo resultado).
template <eng::u16 Max>
bool draw_text_blit(eng::field::Surface& s, eng::graphics::FramePlan& plan, GlyphCache<Max>& cache,
		    eng::s32 x, eng::s32 y, const char* text, eng::u8 color,
		    eng::Span<eng::u16> src_scratch, eng::u8 planes) noexcept {
	if (text == nullptr || planes == 0u) {
		// Nada que pintar; el clip/alineación se comprueban al emitir.
		return text != nullptr;
	}
	if ((x & 15) != 0) {
		return false;
	}
	if (src_scratch.size() < static_cast<eng::u32>(planes) * eng::Font8::kRows) {
		return false;
	}
	// Decodifica el texto a code points, en pares (una palabra = 2 glifos).
	const eng::u8* q = reinterpret_cast<const eng::u8*>(text);
	eng::s32 cx = x;
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
		// Máscara del par: glifo 0 en la mitad alta, glifo 1 en la baja (MSB = izquierda).
		eng::u16 mask_rows[eng::Font8::kRows] {};
		for (eng::u8 r = 0u; r < eng::Font8::kRows; ++r) {
			mask_rows[r] = static_cast<eng::u16>(m0_rows[r] | (m1_rows[r] >> 8));
		}
		for (eng::u8 p = 0u; p < planes; ++p) {
			eng::u16* base = src_scratch.data() + static_cast<eng::u32>(p) * eng::Font8::kRows;
			solid_color_plane(base, color, p);
		}
		// Un solo `blit_masked` con TODOS los planos: el destino recorre sus planos contiguos
		// (mientras que varios blits de 1 plano escribirían siempre el plano 0).
		const bool ok = s.blit_masked(plan,
					      eng::Span<const eng::u16>(src_scratch.data(),
									static_cast<eng::u32>(planes) * eng::Font8::kRows),
					      eng::Span<const eng::u16>(mask_rows, eng::Font8::kRows),
					      cx, y, 16u, 8u, 2u, eng::Font8::kRows * 2u, planes);
		if (!ok) {
			return false;
		}
		if (cp1 == 0u) {
			return true;
		}
		cx = static_cast<eng::s32>(cx + 16);
	}
}

} // namespace eng::graphics
