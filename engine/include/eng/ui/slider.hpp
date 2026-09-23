#pragma once

/// \file slider.hpp
/// **`eng::ui::Slider`** (G?): control deslizante sobre un `s16*` externo (`min..max`), sin heap.
/// Se ajusta con click/arrastre (posición → valor) y, con foco, con `Left`/`Right`. Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §11.

#include <eng/core/types/types.hpp>
#include <eng/ui/event.hpp>
#include <eng/ui/keys.hpp>
#include <eng/ui/painter.hpp>
#include <eng/ui/theme.hpp>
#include <eng/ui/widget.hpp>

namespace eng::ui {

struct Slider : Widget {
	/// Deslizador horizontal sobre `[min, max]`; acepta foco (flechas).
	Slider() noexcept {
		type = WidgetType::Slider;
		set_flag(WfAcceptsFocus);
	}

	eng::s16* value = nullptr;
	eng::s16 min = 0;
	eng::s16 max = 100;
	void (*on_change)(void* user) = nullptr;
	void* user = nullptr;

	/// Fija `*value` a partir de la coordenada `x` (mapea el ancho útil al rango).
	void set_from_x(eng::s16 x) noexcept {
		const eng::s16 w = static_cast<eng::s16>(bounds.w > 1u ? bounds.w - 1u : 1u);
		eng::s16 rel = static_cast<eng::s16>(x - bounds.x);
		if (rel < 0) {
			rel = 0;
		}
		if (rel > w) {
			rel = w;
		}
		const eng::s16 v = static_cast<eng::s16>(
			min + static_cast<eng::s32>(static_cast<eng::s32>(max) - min) * rel / w);
		if (value != nullptr) {
			*value = v;
		}
	}

	/// Ajusta `*value` en `delta`, acotado a `[min, max]`.
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

/// Dibuja la pista (hundida) y el pomo en la posición del valor.
inline void draw_slider(Slider& s, UiPainter& p) {
	if (!s.has(WfVisible)) {
		return;
	}
	p.fill(s.bounds, p.theme().edit_bg);
	p.bevel_in(s.bounds);
	if (s.value == nullptr || s.max <= s.min) {
		return;
	}
	const eng::s16 w = static_cast<eng::s16>(s.bounds.w > 1u ? s.bounds.w - 1u : 1u);
	eng::s32 v = *s.value;
	if (v < s.min) {
		v = s.min;
	}
	if (v > s.max) {
		v = s.max;
	}
	const eng::s16 kx = static_cast<eng::s16>(
		s.bounds.x + static_cast<eng::s16>(v - s.min) * w / (s.max - s.min));
	const Rect knob {kx, s.bounds.y, 3u, s.bounds.h};
	p.fill(knob, p.theme().fill_active);
	p.bevel_out(knob);
}

/// Evento del deslizador: click/arrastre fija el valor; con foco, `Left`/`Right` lo ajustan.
inline bool event_slider(Slider& s, const UiEvent& ev) {
	if (!s.has(WfEnabled)) {
		return false;
	}
	if (ev.kind == UiEventKind::MouseDown && s.bounds.contains(ev.x, ev.y)) {
		s.set_flag(WfPressed);
		s.set_from_x(ev.x);
		s.mark_dirty();
		if (s.on_change != nullptr) {
			s.on_change(s.user);
		}
		return true;
	}
	if (ev.kind == UiEventKind::MouseUp && s.has(WfPressed)) {
		if (s.bounds.contains(ev.x, ev.y)) {
			s.set_from_x(ev.x);
		}
		s.clear_flag(WfPressed);
		s.mark_dirty();
		return true;
	}
	if (ev.kind == UiEventKind::KeyDown && s.has(WfFocused)) {
		switch (ev.key) {
		case kKeyLeft:
		case kKeyDown:
			s.nudge(static_cast<eng::s16>(-1));
			s.mark_dirty();
			return true;
		case kKeyRight:
		case kKeyUp:
			s.nudge(static_cast<eng::s16>(1));
			s.mark_dirty();
			return true;
		default:
			return false;
		}
	}
	return false;
}

} // namespace eng::ui
