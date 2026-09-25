#pragma once

/// \file widgets.hpp
/// **Widgets** de `eng::ui`: `Panel`, `Label` (G1), `Button` (G2) y `CheckBox`/`RadioButton`
/// (G3), con el **despacho por tipo** (dibujo y eventos) y la **medida preferida**. Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §8/§11.
///
/// Decisiones fijadas:
/// - **Despacho**: `switch` exhaustivo sobre `WidgetType` (no tabla de punteros ni `virtual`).
/// - **Construcción**: cada widget tiene un constructor por defecto que fija su `type` (y, si
///   procede, `WfAcceptsFocus`); los campos concretos se asignan después. No se usan *designated
///   initializers* porque el `type` es miembro de la base y no se puede designar.
/// - **Métrica** (`measure`): texto `text_width × 8`; botón `text_width + 2·pad_x × btn_h`;
///   check/radio `lado + pad_x + ancho_etiqueta`; contenedores usan su `bounds`.

#include <eng/core/types/types.hpp>
#include <eng/ui/editbox.hpp>
#include <eng/ui/event.hpp>
#include <eng/ui/layout.hpp>
#include <eng/ui/list.hpp>
#include <eng/ui/painter.hpp>
#include <eng/ui/scroll.hpp>
#include <eng/ui/slider.hpp>
#include <eng/ui/text.hpp>
#include <eng/ui/theme.hpp>
#include <eng/ui/widget.hpp>
#include <eng/ui/window.hpp>

namespace eng::ui {

/// Contenedor: dibuja el panel del tema. No consume eventos.
struct Panel : Widget {
	Panel() noexcept { type = WidgetType::Panel; }
};

/// Etiqueta de texto estático.
struct Label : Widget {
	Label() noexcept { type = WidgetType::Label; }
	const char* text = nullptr;
	eng::u8 color = 0u;          ///< color fijo si `use_theme_color == false`
	bool use_theme_color = true; ///< usar `theme.text` (por defecto)
};

/// Botón: cara + bisel según `WfPressed`; dispara `on_click(user)` al soltar dentro.
struct Button : Widget {
	Button() noexcept {
		type = WidgetType::Button;
		set_flag(WfAcceptsFocus);
	}
	const char* text = nullptr;
	eng::u8 color = 0u;
	bool use_theme_color = true;
	void (*on_click)(void* user) = nullptr;
	void* user = nullptr;
};

/// Casilla: caja + tick; alterna `*value` al soltar dentro.
struct CheckBox : Widget {
	CheckBox() noexcept {
		type = WidgetType::Check;
		set_flag(WfAcceptsFocus);
	}
	const char* label = nullptr;
	bool* value = nullptr;
	void (*on_change)(void* user) = nullptr;
	void* user = nullptr;
};

/// Botón de radio: círculo + punto; activa `*value` y desactiva el grupo (`group_id`).
struct RadioButton : Widget {
	RadioButton() noexcept {
		type = WidgetType::Radio;
		set_flag(WfAcceptsFocus);
	}
	const char* label = nullptr;
	bool* value = nullptr;
	eng::u8 group_id = 0u;
	void (*on_change)(void* user) = nullptr;
	void* user = nullptr;
};

/// Glifo 1-bit de tick (8×8, MSB primero).
inline constexpr eng::u16 kTickGlyph[8] = {
	0x0000u, 0x0001u, 0x0003u, 0x0006u, 0x000cu, 0x0018u, 0x0010u, 0x0000u,
};

// --- Medida preferida ------------------------------------------------------
[[nodiscard]] inline Rect measure(const Widget& w, const UiTheme& th) noexcept {
	switch (w.type) {
	case WidgetType::Label: {
		const auto& l = static_cast<const Label&>(w);
		return Rect {0, 0, text_width(l.text), 8u};
	}
	case WidgetType::Button: {
		const auto& b = static_cast<const Button&>(w);
		return Rect {0, 0,
			     static_cast<eng::u16>(text_width(b.text) +
						   static_cast<eng::u16>(2u) * th.pad_x),
			     th.btn_h};
	}
	case WidgetType::Check: {
		const auto& c = static_cast<const CheckBox&>(w);
		return Rect {0, 0,
			     static_cast<eng::u16>(th.check_s + th.pad_x + text_width(c.label)),
			     th.check_s};
	}
	case WidgetType::Radio: {
		const auto& r = static_cast<const RadioButton&>(w);
		return Rect {0, 0,
			     static_cast<eng::u16>(th.radio_s + th.pad_x + text_width(r.label)),
			     th.radio_s};
	}
	case WidgetType::Panel:
	case WidgetType::Edit:
	case WidgetType::Slider:
	case WidgetType::ScrollBar:
	case WidgetType::List:
	case WidgetType::Window:
		return w.bounds;
	}
	return w.bounds;
}

// --- Dibujo ----------------------------------------------------------------
inline void draw_panel(Panel& pa, UiPainter& p) {
	if (!pa.has(WfVisible)) {
		return;
	}
	p.panel(pa.bounds);
}

/// Dibuja la etiqueta (color del tema o fijo).
inline void draw_label(Label& l, UiPainter& p) {
	if (!l.has(WfVisible)) {
		return;
	}
	const eng::u8 fg = l.use_theme_color ? p.theme().text : l.color;
	p.text(l.bounds.x, l.bounds.y, l.text, fg);
}

/// Dibuja la cara del botón (según `WfPressed`) y su texto centrado.
inline void draw_button(Button& b, UiPainter& p) {
	if (!b.has(WfVisible)) {
		return;
	}
	p.button_face(b.bounds, b.has(WfPressed));
	const eng::u8 fg = b.use_theme_color ? p.theme().text : b.color;
	const eng::u16 tw = text_width(b.text);
	const eng::s16 tx = static_cast<eng::s16>(
		b.bounds.x + (b.bounds.w > tw ? (b.bounds.w - tw) / 2u : 0u));
	const eng::s16 ty = static_cast<eng::s16>(
		b.bounds.y + (b.bounds.h > 8u ? (b.bounds.h - 8u) / 2u : 0u));
	p.text(tx, ty, b.text, fg);
}

/// Dibuja la casilla (caja hundida + tick si `*value`) y su etiqueta.
inline void draw_check(CheckBox& c, UiPainter& p) {
	if (!c.has(WfVisible)) {
		return;
	}
	const eng::u16 s = p.theme().check_s;
	const Rect box {c.bounds.x, c.bounds.y, s, s};
	p.fill(box, p.theme().edit_bg);
	p.bevel_in(box);
	if (c.value != nullptr && *c.value) {
		p.glyph(static_cast<eng::s16>(box.x + 1), static_cast<eng::s16>(box.y + 1),
			kTickGlyph, 8u, 8u, p.theme().text);
	}
	if (c.label != nullptr) {
		p.text(static_cast<eng::s16>(box.x + s + p.theme().pad_x), box.y, c.label,
		       p.theme().text);
	}
}

/// Dibuja el botón de radio (círculo/caja + punto si `*value`) y su etiqueta.
inline void draw_radio(RadioButton& r, UiPainter& p) {
	if (!r.has(WfVisible)) {
		return;
	}
	const eng::u16 s = p.theme().radio_s;
	const Rect box {r.bounds.x, r.bounds.y, s, s};
	p.fill(box, p.theme().edit_bg);
	p.bevel_in(box);
	if (r.value != nullptr && *r.value) {
		p.fill(box.inset(3u), p.theme().focus_ring);
	}
	if (r.label != nullptr) {
		p.text(static_cast<eng::s16>(box.x + s + p.theme().pad_x), box.y, r.label,
		       p.theme().text);
	}
}

/// Despacho de dibujo por tipo (`switch` exhaustivo). Los tipos no implementados no pintan.
inline void draw_widget(Widget& w, UiPainter& p) {
	switch (w.type) {
	case WidgetType::Panel:
		draw_panel(static_cast<Panel&>(w), p);
		break;
	case WidgetType::Label:
		draw_label(static_cast<Label&>(w), p);
		break;
	case WidgetType::Button:
		draw_button(static_cast<Button&>(w), p);
		break;
	case WidgetType::Check:
		draw_check(static_cast<CheckBox&>(w), p);
		break;
	case WidgetType::Radio:
		draw_radio(static_cast<RadioButton&>(w), p);
		break;
	case WidgetType::Edit:
		draw_edit(static_cast<EditBox&>(w), p);
		break;
	case WidgetType::Window:
		draw_window(static_cast<Window&>(w), p);
		break;
	case WidgetType::Slider:
		draw_slider(static_cast<Slider&>(w), p);
		break;
	case WidgetType::ScrollBar:
		draw_scroll_bar(static_cast<ScrollBar&>(w), p);
		break;
	case WidgetType::List:
		draw_list(static_cast<ListView&>(w), p);
		break;
	}
}

// --- Eventos ---------------------------------------------------------------
inline bool event_button(Button& b, const UiEvent& ev) {
	if (!b.has(WfEnabled)) {
		return false;
	}
	if (ev.kind == UiEventKind::MouseDown && b.bounds.contains(ev.x, ev.y)) {
		b.set_flag(WfPressed);
		b.mark_dirty();
		return true;
	}
	if (ev.kind == UiEventKind::MouseUp && b.has(WfPressed)) {
		const bool inside = b.bounds.contains(ev.x, ev.y);
		b.clear_flag(WfPressed);
		b.mark_dirty();
		if (inside && b.on_click != nullptr) {
			b.on_click(b.user);
		}
		return true;
	}
	return false;
}

/// Evento de la casilla: alterna `*value` al soltar dentro.
inline bool event_check(CheckBox& c, const UiEvent& ev) {
	if (!c.has(WfEnabled)) {
		return false;
	}
	if (ev.kind == UiEventKind::MouseDown && c.bounds.contains(ev.x, ev.y)) {
		c.set_flag(WfPressed);
		c.mark_dirty();
		return true;
	}
	if (ev.kind == UiEventKind::MouseUp && c.has(WfPressed)) {
		const bool inside = c.bounds.contains(ev.x, ev.y);
		c.clear_flag(WfPressed);
		c.mark_dirty();
		if (inside) {
			if (c.value != nullptr) {
				*c.value = !*c.value;
			}
			if (c.on_change != nullptr) {
				c.on_change(c.user);
			}
		}
		return true;
	}
	return false;
}

/// Activa `r` y desactiva sus hermanos de radio del mismo `group_id`.
inline void radio_activate(RadioButton& r) {
	if (r.parent != nullptr) {
		for (Widget* c = r.parent->first_child; c != nullptr; c = c->next) {
			if (c->type != WidgetType::Radio) {
				continue;
			}
			auto& s = static_cast<RadioButton&>(*c);
			if (s.group_id != r.group_id) {
				continue;
			}
			if (s.value != nullptr) {
				*s.value = (c == &r);
			}
			s.mark_dirty();
		}
	} else if (r.value != nullptr) {
		*r.value = true;
		r.mark_dirty();
	}
}

/// Evento del radio: activa su grupo al soltar dentro.
inline bool event_radio(RadioButton& r, const UiEvent& ev) {
	if (!r.has(WfEnabled)) {
		return false;
	}
	if (ev.kind == UiEventKind::MouseDown && r.bounds.contains(ev.x, ev.y)) {
		r.set_flag(WfPressed);
		r.mark_dirty();
		return true;
	}
	if (ev.kind == UiEventKind::MouseUp && r.has(WfPressed)) {
		const bool inside = r.bounds.contains(ev.x, ev.y);
		r.clear_flag(WfPressed);
		if (inside) {
			radio_activate(r);
			if (r.on_change != nullptr) {
				r.on_change(r.user);
			}
		}
		r.mark_dirty();
		return true;
	}
	return false;
}

/// Despacho de eventos por tipo. Devuelve `true` si el widget consumió el evento.
inline bool event_widget(Widget& w, const UiEvent& ev) {
	switch (w.type) {
	case WidgetType::Button:
		return event_button(static_cast<Button&>(w), ev);
	case WidgetType::Check:
		return event_check(static_cast<CheckBox&>(w), ev);
	case WidgetType::Radio:
		return event_radio(static_cast<RadioButton&>(w), ev);
	case WidgetType::Edit:
		return event_edit(static_cast<EditBox&>(w), ev);
	case WidgetType::Slider:
		return event_slider(static_cast<Slider&>(w), ev);
	case WidgetType::ScrollBar:
		return event_scroll_bar(static_cast<ScrollBar&>(w), ev);
	case WidgetType::List:
		return event_list(static_cast<ListView&>(w), ev);
	case WidgetType::Panel:
	case WidgetType::Label:
	case WidgetType::Window:
		return false;
	}
	return false;
}

/// Dibuja `w` y su subárbol de **atrás hacia delante** (hijos en orden de creación, para que el
/// más nuevo —al frente— quede encima). Útil para componer el árbol sobre una `Surface`.
inline void draw_tree(Widget& w, UiPainter& p) {
	if (!w.has(WfVisible)) {
		return;
	}
	draw_widget(w, p);
	Widget* kids[kMaxLayoutChildren];
	const eng::u8 n = children_creation_order<kMaxLayoutChildren>(w, kids);
	for (eng::u8 i = 0u; i < n; ++i) {
		draw_tree(*kids[i], p);
	}
}

} // namespace eng::ui
