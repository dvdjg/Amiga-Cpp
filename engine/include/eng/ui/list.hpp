#pragma once

/// \file list.hpp
/// **`eng::ui::ListView`**: lista de cadenas con **selección** y **desplazamiento vertical**
/// (lógico: solo se pintan las filas visibles). El índice seleccionado es externo (`s16*`,
/// `-1` = ninguno); el desplazamiento (`top`) lo gestiona la lista y se puede enlazar con un
/// `ScrollBar`. Con foco: flechas (1) y `Shift`+flecha (página), `Home`/`End`. Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §11.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/ui/event.hpp>
#include <eng/ui/keys.hpp>
#include <eng/ui/painter.hpp>
#include <eng/ui/text.hpp>
#include <eng/ui/theme.hpp>
#include <eng/ui/widget.hpp>

namespace eng::ui {

/// Alto de fila por defecto (texto de 8 px + 2 de margen).
inline constexpr eng::u8 kListItemH = 10u;

struct ListView : Widget {
	/// Lista sobre una vista de cadenas (`items[0..count-1]`); acepta foco.
	ListView() noexcept {
		type = WidgetType::List;
		set_flag(WfAcceptsFocus);
	}

	eng::Span<const char* const> items {}; ///< cadenas (no propietario; capacidad fija)
	eng::u16 count = 0u;
	eng::s16* selected = nullptr; ///< índice externo (`-1` = ninguno)
	eng::u8 item_h = kListItemH;
	void (*on_select)(void* user) = nullptr;
	void* user = nullptr;

	eng::s16 top = 0; ///< primera fila visible (desplazamiento)

	/// Filas que caben en el alto actual.
	[[nodiscard]] eng::s16 visible_rows() const noexcept {
		const eng::s16 ih = static_cast<eng::s16>(item_h != 0u ? item_h : 1u);
		return static_cast<eng::s16>(bounds.h / static_cast<eng::u16>(ih));
	}

	/// Acota `top` a `[0, count - visible_rows]`.
	void clamp_top() noexcept {
		eng::s16 max_top = static_cast<eng::s16>(count) - visible_rows();
		if (max_top < 0) {
			max_top = 0;
		}
		if (top < 0) {
			top = 0;
		}
		if (top > max_top) {
			top = max_top;
		}
	}

	/// Ajusta `top` para que la fila seleccionada quede visible.
	void ensure_visible() noexcept {
		if (selected == nullptr || *selected < 0) {
			return;
		}
		const eng::s16 rows = visible_rows();
		if (*selected < top) {
			top = *selected;
		} else if (*selected >= static_cast<eng::s16>(top + rows)) {
			top = static_cast<eng::s16>(*selected - rows + 1);
		}
		clamp_top();
	}

	/// Selecciona `i` (si está en rango) y lo hace visible.
	void select(eng::s16 i) noexcept {
		if (i < 0 || i >= static_cast<eng::s16>(count)) {
			return;
		}
		if (selected != nullptr) {
			*selected = i;
		}
		ensure_visible();
	}
};

/// Pinta el marco y las filas visibles (la seleccionada con `fill_active`).
inline void draw_list(ListView& l, UiPainter& p) {
	if (!l.has(WfVisible)) {
		return;
	}
	p.fill(l.bounds, p.theme().edit_bg);
	p.bevel_in(l.bounds);
	if (l.items.empty() || l.count == 0u) {
		return;
	}
	l.clamp_top();
	const eng::s16 rows = l.visible_rows();
	const eng::s16 ih = static_cast<eng::s16>(l.item_h != 0u ? l.item_h : 1u);
	const eng::s16 sel = (l.selected != nullptr) ? *l.selected : static_cast<eng::s16>(-1);
	for (eng::s16 r = 0; r < rows; ++r) {
		const eng::s16 idx = static_cast<eng::s16>(l.top + r);
		if (idx >= static_cast<eng::s16>(l.count)) {
			break;
		}
		const eng::s16 ry = static_cast<eng::s16>(l.bounds.y + 1 + r * ih);
		if (ry + 8 > l.bounds.bottom()) {
			break;
		}
		if (idx == sel) {
			const Rect hi {static_cast<eng::s16>(l.bounds.x + 1),
				       static_cast<eng::s16>(ry - 1),
				       static_cast<eng::u16>(l.bounds.w - 2u),
				       l.item_h};
			p.fill(hi, p.theme().fill_active);
		}
		p.text(static_cast<eng::s16>(l.bounds.x + p.theme().pad_x), ry, l.items[idx],
		       p.theme().text);
	}
}

/// Evento de la lista: click selecciona la fila; con foco, flechas/página/`Home`/`End`.
inline bool event_list(ListView& l, const UiEvent& ev) {
	if (!l.has(WfEnabled)) {
		return false;
	}
	if (ev.kind == UiEventKind::MouseDown && l.bounds.contains(ev.x, ev.y)) {
		const eng::s16 ih = static_cast<eng::s16>(l.item_h != 0u ? l.item_h : 1u);
		eng::s16 row = static_cast<eng::s16>((ev.y - l.bounds.y - 1) / ih);
		if (row < 0) {
			row = 0;
		}
		const eng::s16 idx = static_cast<eng::s16>(l.top + row);
		l.set_flag(WfPressed);
		l.mark_dirty();
		if (idx >= 0 && idx < static_cast<eng::s16>(l.count)) {
			l.select(idx);
			if (l.on_select != nullptr) {
				l.on_select(l.user);
			}
		}
		return true;
	}
	if (ev.kind == UiEventKind::MouseUp && l.has(WfPressed)) {
		l.clear_flag(WfPressed);
		l.mark_dirty();
		return true;
	}
	if (ev.kind == UiEventKind::KeyDown && l.has(WfFocused)) {
		if (l.selected == nullptr) {
			return false;
		}
		const eng::s16 step = ev.shift ? l.visible_rows() : static_cast<eng::s16>(1);
		switch (ev.key) {
		case kKeyUp:
			l.select(static_cast<eng::s16>(*l.selected - step));
			break;
		case kKeyDown:
			l.select(static_cast<eng::s16>(*l.selected + step));
			break;
		case kKeyHome:
			l.select(0);
			break;
		case kKeyEnd:
			l.select(static_cast<eng::s16>(l.count - 1u));
			break;
		default:
			return false;
		}
		l.mark_dirty();
		if (l.on_select != nullptr) {
			l.on_select(l.user);
		}
		return true;
	}
	return false;
}

} // namespace eng::ui
