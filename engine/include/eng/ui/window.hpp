#pragma once

/// \file window.hpp
/// **Ventanas** de `eng::ui` (G6): `Window` (marco + título + contenido, no modal), `Popup`
/// (se cierra al pulsar fuera o con `Esc`), `Toast` (efímero, TTL en frames, **no capta input**)
/// y `Dialog` (modal: filtra hit-test y foco). Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §13.
///
/// Las ventanas son widgets: viven en el árbol (hijos del escritorio/raíz) y su orden en la lista
/// es el orden Z (`raise` las sube al frente). La política (modalidad, cerrar popups, TTL) la
/// aplica `UiContext` (`context.hpp`).

#include <eng/core/types.hpp>
#include <eng/ui/painter.hpp>
#include <eng/ui/theme.hpp>
#include <eng/ui/widget.hpp>

namespace eng::ui {

/// Clase de ventana.
enum class WindowKind : eng::u8 {
	Window, ///< normal, no modal
	Popup,  ///< se cierra al pulsar fuera (o `Esc`)
	Toast,  ///< efímero; TTL en frames; no capta input
	Dialog, ///< modal
};

/// Ventana: contenedor con marco y, opcionalmente, barra de título.
struct Window : Widget {
	Window() noexcept { type = WidgetType::Window; }

	WindowKind kind = WindowKind::Window;
	const char* title = nullptr;
	eng::u16 ttl = 0u;            ///< Toast: frames que le quedan
	bool close_on_outside = true; ///< Popup
};

/// Fija el tipo de ventana y los flags derivados (`Dialog` → modal; `Toast` → sin input).
inline void window_set_kind(Window& w, WindowKind k) noexcept {
	w.kind = k;
	if (k == WindowKind::Dialog) {
		w.set_flag(WfModal);
	} else {
		w.clear_flag(WfModal);
	}
	if (k == WindowKind::Toast) {
		w.set_flag(WfNoInput);
	} else {
		w.clear_flag(WfNoInput);
	}
}

/// Dibuja el marco (`panel`) y la barra de título si la hay.
inline void draw_window(Window& w, UiPainter& p) {
	if (!w.has(WfVisible)) {
		return;
	}
	p.panel(w.bounds);
	if (w.title != nullptr && w.bounds.h >= 12u) {
		const Rect bar {w.bounds.x, w.bounds.y, w.bounds.w, 11u};
		p.fill(bar, p.theme().fill_active);
		p.text(static_cast<eng::s16>(bar.x + p.theme().pad_x),
		       static_cast<eng::s16>(bar.y + 2), w.title, p.theme().text);
	}
}

} // namespace eng::ui
