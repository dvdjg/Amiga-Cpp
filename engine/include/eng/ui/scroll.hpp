#pragma once

/// \file scroll.hpp
/// **`eng::ui::ScrollBar`**: barra de desplazamiento sobre un `s16*` externo (`min..max`), con
/// **pomo** de longitud proporcional a la página visible. Se ajusta con click (posición → valor)
/// y, con foco, con flechas (paso de 1; con `Shift`, de una página) y `Home`/`End`. Horizontal o
/// vertical. Ver `docs/engine/architecture/GUI_LIBRARY.md` §11.
///
/// No mueve contenido por sí misma: fija el índice/desplazamiento (`*value`) que el contenedor
/// (p. ej. `ListView`) usa para decidir qué filas pintar. Así el desplazamiento es lógico y
/// testeable sin tocar el rasterizador.

#include <eng/core/types/types.hpp>
#include <eng/ui/event.hpp>
#include <eng/ui/keys.hpp>
#include <eng/ui/painter.hpp>
#include <eng/ui/theme.hpp>
#include <eng/ui/widget.hpp>

namespace eng::ui {

/// Longitud mínima del pomo (px), para que siga siendo visible con contenidos muy grandes.
inline constexpr eng::s16 kScrollBarMinThumb = 6;

struct ScrollBar : Widget {
	/// Barra sobre `[min, max]`; acepta foco (flechas/`Home`/`End`).
	ScrollBar() noexcept {
		type = WidgetType::ScrollBar;
		set_flag(WfAcceptsFocus);
	}

	eng::s16* value = nullptr;
	eng::s16 min = 0;
	eng::s16 max = 100;
	eng::s16 page = 10;    ///< unidades por página (alto del pomo en unidades)
	bool vertical = true;  ///< `true` = barra vertical; `false` = horizontal
	void (*on_change)(void* user) = nullptr;
	void* user = nullptr;

	/// Longitud de la pista (px) según la orientación.
	[[nodiscard]] eng::s16 track_len() const noexcept {
		return vertical ? static_cast<eng::s16>(bounds.h)
				: static_cast<eng::s16>(bounds.w);
	}

	/// Rango de valores (`max - min`), acotado a 0.
	[[nodiscard]] eng::s16 span() const noexcept {
		return static_cast<eng::s16>(max > min ? max - min : 0);
	}

	/// Longitud del pomo (px): proporcional a `page / (span + page)`, con mínimo.
	[[nodiscard]] eng::s16 thumb_len() const noexcept {
		const eng::s16 len = track_len();
		if (len <= 0) {
			return 0;
		}
		if (span() <= 0) {
			return len;
		}
		eng::s32 t = static_cast<eng::s32>(page) * len / (span() + page);
		if (t < kScrollBarMinThumb) {
			t = kScrollBarMinThumb;
		}
		if (t > len) {
			t = len;
		}
		return static_cast<eng::s16>(t);
	}

	/// Posición del pomo (px) desde el inicio de la pista.
	[[nodiscard]] eng::s16 thumb_pos() const noexcept {
		const eng::s16 len = track_len();
		const eng::s16 t = thumb_len();
		if (value == nullptr || span() <= 0 || len <= t) {
			return 0;
		}
		eng::s16 v = *value;
		if (v < min) {
			v = min;
		}
		if (v > max) {
			v = max;
		}
		return static_cast<eng::s16>(static_cast<eng::s32>(len - t) * (v - min) / span());
	}

	/// Fija `*value` a partir de una coordenada (píxel) a lo largo de la pista, centrando el pomo.
	void set_from_pos(eng::s16 p) noexcept {
		const eng::s16 len = track_len();
		const eng::s16 t = thumb_len();
		if (value == nullptr || span() <= 0 || len <= t) {
			return;
		}
		eng::s16 rel = static_cast<eng::s16>(p - (vertical ? bounds.y : bounds.x));
		rel = static_cast<eng::s16>(rel - t / 2);
		const eng::s16 room = static_cast<eng::s16>(len - t);
		if (rel < 0) {
			rel = 0;
		}
		if (rel > room) {
			rel = room;
		}
		*value = static_cast<eng::s16>(min + static_cast<eng::s32>(span()) * rel / room);
	}

	/// Desplaza `*value` en `delta`, acotado a `[min, max]`.
	void nudge(eng::s16 delta) noexcept {
		if (value == nullptr) {
			return;
		}
		eng::s32 v = static_cast<eng::s32>(*value) + delta;
		if (v < min) {
			v = min;
		}
		if (v > max) {
			v = max;
		}
		*value = static_cast<eng::s16>(v);
	}
};

/// Pinta la pista (hundida) y el pomo en la posición del valor.
inline void draw_scroll_bar(ScrollBar& s, UiPainter& p) {
	if (!s.has(WfVisible)) {
		return;
	}
	p.fill(s.bounds, p.theme().edit_bg);
	p.bevel_in(s.bounds);
	const eng::s16 t = s.thumb_len();
	if (t <= 0) {
		return;
	}
	const eng::s16 pos = s.thumb_pos();
	if (s.vertical) {
		const Rect thumb {s.bounds.x,
				  static_cast<eng::s16>(s.bounds.y + pos),
				  s.bounds.w,
				  static_cast<eng::u16>(t)};
		p.fill(thumb, p.theme().fill_active);
		p.bevel_out(thumb);
	} else {
		const Rect thumb {static_cast<eng::s16>(s.bounds.x + pos),
				  s.bounds.y,
				  static_cast<eng::u16>(t),
				  s.bounds.h};
		p.fill(thumb, p.theme().fill_active);
		p.bevel_out(thumb);
	}
}

/// Evento de la barra: click fija el valor; con foco, flechas (1) y `Shift`+flecha (página).
inline bool event_scroll_bar(ScrollBar& s, const UiEvent& ev) {
	if (!s.has(WfEnabled)) {
		return false;
	}
	if (ev.kind == UiEventKind::MouseDown && s.bounds.contains(ev.x, ev.y)) {
		s.set_flag(WfPressed);
		s.set_from_pos(s.vertical ? ev.y : ev.x);
		s.mark_dirty();
		if (s.on_change != nullptr) {
			s.on_change(s.user);
		}
		return true;
	}
	if (ev.kind == UiEventKind::MouseUp && s.has(WfPressed)) {
		if (s.bounds.contains(ev.x, ev.y)) {
			s.set_from_pos(s.vertical ? ev.y : ev.x);
		}
		s.clear_flag(WfPressed);
		s.mark_dirty();
		return true;
	}
	if (ev.kind == UiEventKind::KeyDown && s.has(WfFocused)) {
		const eng::s16 page = static_cast<eng::s16>(s.page > 0 ? s.page : 1);
		const eng::s16 step = ev.shift ? page : static_cast<eng::s16>(1);
		switch (ev.key) {
		case kKeyUp:
			if (!s.vertical) {
				return false;
			}
			s.nudge(static_cast<eng::s16>(-step));
			break;
		case kKeyDown:
			if (!s.vertical) {
				return false;
			}
			s.nudge(step);
			break;
		case kKeyLeft:
			if (s.vertical) {
				return false;
			}
			s.nudge(static_cast<eng::s16>(-step));
			break;
		case kKeyRight:
			if (s.vertical) {
				return false;
			}
			s.nudge(step);
			break;
		case kKeyHome:
			s.nudge(static_cast<eng::s16>(-0x7fff));
			break;
		case kKeyEnd:
			s.nudge(static_cast<eng::s16>(0x7fff));
			break;
		default:
			return false;
		}
		s.mark_dirty();
		if (s.on_change != nullptr) {
			s.on_change(s.user);
		}
		return true;
	}
	return false;
}

} // namespace eng::ui
