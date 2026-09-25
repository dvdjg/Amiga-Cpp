#pragma once

/// \file painter.hpp
/// **`eng::ui::UiPainter`**: chrome de UI sobre `field::Surface`. No dibuja píxeles: delega en
/// `Surface`, que enruta por `Rasterizer` (CPU/Blitter) y recorta contra el clip. Aporta las
/// operaciones con concepto de UI (marcos, biseles, paneles, texto con fondo, glifos). Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §5.

#include <eng/core/types/box.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/glyph_cache.hpp>
#include <eng/ui/paint_target.hpp>
#include <eng/ui/text.hpp>
#include <eng/ui/theme.hpp>

namespace eng::graphics {
class FramePlan;
}

namespace eng::ui {

/// Dibuja chrome de UI sobre una `Surface` (pantalla o backing de ventana) con un `UiTheme`.
class UiPainter {
public:
	/// Pinta sobre una `Surface` (atajo de `PaintTarget::from_surface`: recorte natural, píxel
	/// cuadrado).
	UiPainter(eng::field::Surface& surface, eng::Ref<eng::graphics::FramePlan> plan,
		  const UiTheme& theme) noexcept
		: UiPainter(PaintTarget::from_surface(surface), plan, theme) {}

	/// Pinta sobre un destino cualquiera (`Surface` hoy; `RastPort` con `-DENG_UI_INTUITION`).
	UiPainter(PaintTarget target, eng::Ref<eng::graphics::FramePlan> plan, const UiTheme& theme) noexcept
		: m_target(target)
		, m_plan(plan)
		, m_theme(theme) {}

	[[nodiscard]] const PaintTarget& target() const noexcept { return m_target; }
	[[nodiscard]] const Rect& clip() const noexcept { return m_target.clip; }
	[[nodiscard]] const UiTheme& theme() const noexcept { return m_theme; }
	[[nodiscard]] eng::field::Surface& surface() noexcept { return *m_target.surface; }

	// --- Primitivas (delegan en Surface/Rasterizer) ---
	void fill(Rect r, eng::u8 color) {
		m_target.surface->fill_rect(r.x, r.y, r.w, r.h, color);
	}
	void hline(eng::s16 x0, eng::s16 x1, eng::s16 y, eng::u8 color) {
		m_target.surface->draw_line(x0, y, x1, y, color, m_plan);
	}
	void vline(eng::s16 x, eng::s16 y0, eng::s16 y1, eng::u8 color) {
		m_target.surface->draw_line(x, y0, x, y1, color, m_plan);
	}
	void frame(Rect r, eng::u8 color);
	void bevel_out(Rect r); ///< relieve: shine arriba/izquierda, shadow abajo/derecha
	void bevel_in(Rect r);  ///< hundido: shadow arriba/izquierda, shine abajo/derecha
	void panel(Rect r);     ///< relleno + marco según `theme().panel_frame`
	void button_face(Rect r, bool pressed);

	// --- Texto (reusa Font8; nunca redibuja fuentes) ---
	void text(eng::s16 x, eng::s16 y, const char* s, eng::u8 fg) {
		m_target.surface->draw_text(x, y, s, fg);
	}
	void text_bg(eng::s16 x, eng::s16 y, const char* s, eng::u8 fg, eng::u8 bg);
	/// Un solo code point (lo usa `draw_text_clipped`).
	void codepoint(eng::s16 x, eng::s16 y, eng::u32 cp, eng::u8 fg) {
		m_target.surface->draw_codepoints(x, y, &cp, 1u, fg);
	}

	/// Texto por **Blitter con caché de glifos** (acelerado). Requiere un `FramePlan` y `x`
	/// múltiplo de 16; la caché y el buffer de trabajo son del llamador (sin heap). Equivalente a
	/// `text` (mismo resultado); ver `eng/graphics/glyph_cache.hpp`.
	bool text_blit(eng::s16 x, eng::s16 y, const char* s, eng::u8 fg,
		       eng::graphics::GlyphCache<>& cache, eng::Span<eng::u16> scratch, eng::u8 planes,
		       Rect clip = {}) {
		if (!m_plan.valid()) {
			return false;
		}
		return eng::graphics::draw_text_blit(*m_target.surface, *m_plan.get(), cache, x, y, s,
						     fg, scratch, planes, clip);
	}

	/// Glifo 1-bit `w × h` desde filas `bits[row]` (bit `w-1` = columna 0, MSB primero).
	void glyph(eng::s16 x, eng::s16 y, const eng::u16* bits, eng::u16 w, eng::u16 h,
		   eng::u8 fg);

private:
	PaintTarget m_target {};
	eng::Ref<eng::graphics::FramePlan> m_plan {};
	const UiTheme& m_theme;
};

/// Borde de 1 px: las cuatro líneas de `r` con `color`.
inline void UiPainter::frame(Rect r, eng::u8 color) {
	if (r.empty()) {
		return;
	}
	const eng::s16 x1 = r.right();
	const eng::s16 y1 = r.bottom();
	hline(r.x, x1, r.y, color);
	hline(r.x, x1, y1, color);
	vline(r.x, r.y, y1, color);
	vline(x1, r.y, y1, color);
}

/// Relieve: shine arriba/izquierda, shadow abajo/derecha.
inline void UiPainter::bevel_out(Rect r) {
	if (r.empty()) {
		return;
	}
	const eng::s16 x1 = r.right();
	const eng::s16 y1 = r.bottom();
	hline(r.x, x1, r.y, m_theme.shine);   // arriba
	vline(r.x, r.y, y1, m_theme.shine);  // izquierda
	hline(r.x, x1, y1, m_theme.shadow);  // abajo
	vline(x1, r.y, y1, m_theme.shadow);  // derecha
}

/// Hundido: shadow arriba/izquierda, shine abajo/derecha.
inline void UiPainter::bevel_in(Rect r) {
	if (r.empty()) {
		return;
	}
	const eng::s16 x1 = r.right();
	const eng::s16 y1 = r.bottom();
	hline(r.x, x1, r.y, m_theme.shadow);
	vline(r.x, r.y, y1, m_theme.shadow);
	hline(r.x, x1, y1, m_theme.shine);
	vline(x1, r.y, y1, m_theme.shine);
}

/// Panel: relleno con `theme().fill` y el marco que indique `theme().panel_frame`.
inline void UiPainter::panel(Rect r) {
	if (r.empty()) {
		return;
	}
	fill(r, m_theme.fill);
	switch (m_theme.panel_frame) {
	case FrameStyle::Flat:
		break;
	case FrameStyle::Raised:
		bevel_out(r);
		break;
	case FrameStyle::Recessed:
		bevel_in(r);
		break;
	case FrameStyle::Double:
		frame(r, m_theme.shadow);
		frame(r.inset(1), m_theme.shine);
		break;
	}
}

/// Cara de botón: relleno normal/activo y bisel hundido si está pulsado.
inline void UiPainter::button_face(Rect r, bool pressed) {
	if (r.empty()) {
		return;
	}
	fill(r, pressed ? m_theme.fill_active : m_theme.fill);
	if (m_theme.button_frame == FrameStyle::Flat) {
		return;
	}
	if (pressed) {
		bevel_in(r);
	} else {
		bevel_out(r);
	}
}

/// Texto con fondo: rellena el rect (medido con `text_width`) y dibuja el texto encima.
inline void UiPainter::text_bg(eng::s16 x, eng::s16 y, const char* s, eng::u8 fg,
			       eng::u8 bg) {
	const eng::u16 w = text_width(s);
	if (w != 0u) {
		fill(Rect { x, y, w, 8u }, bg);
	}
	m_target.surface->draw_text(x, y, s, fg);
}

/// Glifo 1-bit por píxel (`set_pixel`): glifos de UI pequeños (tick, flechas, radio).
inline void UiPainter::glyph(eng::s16 x, eng::s16 y, const eng::u16* bits, eng::u16 w,
			     eng::u16 h, eng::u8 fg) {
	if (bits == nullptr || w == 0u || w > 16u) {
		return;
	}
	for (eng::u16 row = 0u; row < h; ++row) {
		const eng::u16 b = bits[row];
		for (eng::u16 col = 0u; col < w; ++col) {
			if (((b >> (w - 1u - col)) & 1u) != 0u) {
				m_target.surface->set_pixel(x + static_cast<eng::s16>(col),
						    y + static_cast<eng::s16>(row), fg);
			}
		}
	}
}

} // namespace eng::ui
