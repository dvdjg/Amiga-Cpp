#pragma once

/// \file widgets.hpp
/// **Widgets mínimos** de `eng::ui` (G1): `Panel` y `Label`, con el **despacho por tipo** y la
/// **medida preferida**. Ver `docs/engine/architecture/GUI_LIBRARY.md` §8/§11.
///
/// Decisión de despacho (G1): `switch` exhaustivo sobre `WidgetType`, no tabla de punteros. El
/// compilador avisa (`-Wswitch`) de un tipo nuevo sin rama, el código es predecible en el 68000
/// y el compilador puede plegarlo; no se usa `virtual` en el camino caliente.

#include <eng/core/types.hpp>
#include <eng/ui/painter.hpp>
#include <eng/ui/text.hpp>
#include <eng/ui/theme.hpp>
#include <eng/ui/widget.hpp>

namespace eng::ui {

/// Contenedor: dibuja el panel del tema. No consume eventos.
struct Panel : Widget {
	Panel() noexcept { type = WidgetType::Panel; }
};

/// Etiqueta de texto estático.
struct Label : Widget {
	Label() noexcept { type = WidgetType::Label; }
	const char* text = nullptr;
	eng::u8 color = 0u;              ///< color fijo si `use_theme_color == false`
	bool use_theme_color = true;     ///< usar `theme.text` (por defecto)
};

// --- Medida preferida ------------------------------------------------------
// Métricas fijadas en G1: el texto mide `text_width(s) × 8` (alto de la fuente `Font8`). Los
// contenedores no tienen tamaño intrínseco (usan su `bounds`). Los botones (G2) medirán
// `text_width(text) + 2*pad_x` × `theme.btn_h`; los campos y listas, con `pad_x`/`pad_y`.
[[nodiscard]] inline Rect measure(const Widget& w) noexcept {
	switch (w.type) {
	case WidgetType::Label: {
		const auto& l = static_cast<const Label&>(w);
		return Rect {0, 0, text_width(l.text), 8u};
	}
	case WidgetType::Panel:
	case WidgetType::Button:
	case WidgetType::Check:
	case WidgetType::Radio:
	case WidgetType::Edit:
	case WidgetType::Slider:
	case WidgetType::List:
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

inline void draw_label(Label& l, UiPainter& p) {
	if (!l.has(WfVisible)) {
		return;
	}
	const eng::u8 fg = l.use_theme_color ? p.theme().text : l.color;
	p.text(l.bounds.x, l.bounds.y, l.text, fg);
}

/// Despacho de dibujo por tipo (`switch` exhaustivo). Los tipos aún no implementados no pintan.
inline void draw_widget(Widget& w, UiPainter& p) {
	switch (w.type) {
	case WidgetType::Panel:
		draw_panel(static_cast<Panel&>(w), p);
		break;
	case WidgetType::Label:
		draw_label(static_cast<Label&>(w), p);
		break;
	case WidgetType::Button:
	case WidgetType::Check:
	case WidgetType::Radio:
	case WidgetType::Edit:
	case WidgetType::Slider:
	case WidgetType::List:
		break; // pendientes (G2+): sin dibujo
	}
}

} // namespace eng::ui
