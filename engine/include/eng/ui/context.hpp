#pragma once

/// \file context.hpp
/// **`eng::ui::UiContext`**: hit-test, foco y despacho de eventos sobre el árbol de widgets.
/// No lee hardware: consume `UiEvent` (del puente `eng::os::Msg` o de entrada síncrona). Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §10.
///
/// El hit-test recorre el árbol de **delante hacia atrás** (el primer hijo de la lista es el más
/// al frente) y devuelve el primer widget `Visible|Enabled` que contiene el punto. El ratón
/// engancha un widget en `MouseDown` y le entrega el `MouseUp` (aunque el puntero se haya ido
/// fuera), de modo que un botón solo dispara si se suelta dentro.

#include <eng/core/ptr.hpp>
#include <eng/core/types.hpp>
#include <eng/ui/event.hpp>
#include <eng/ui/widget.hpp>
#include <eng/ui/widgets.hpp>

namespace eng::ui {

class UiContext {
public:
	Widget* root = nullptr;
	Widget* focus = nullptr;

	void set_root(Widget* r) noexcept { root = r; }

	/// Devuelve el widget más al frente que contiene `(px, py)` (o `nullptr`).
	[[nodiscard]] Widget* hit_test(Widget* node, eng::s16 px, eng::s16 py) const noexcept {
		if (node == nullptr || !node->has(WfVisible)) {
			return nullptr;
		}
		for (Widget* c = node->first_child; c != nullptr; c = c->next) {
			if (Widget* h = hit_test(c, px, py); h != nullptr) {
				return h;
			}
		}
		if (node->has(WfEnabled) && node->bounds.contains(px, py)) {
			return node;
		}
		return nullptr;
	}

	/// Despacha `ev`. Devuelve `true` si algún widget lo consumió.
	bool dispatch(const UiEvent& ev) {
		if (root == nullptr) {
			return false;
		}
		switch (ev.kind) {
		case UiEventKind::MouseDown: {
			Widget* h = hit_test(root, ev.x, ev.y);
			if (h == nullptr) {
				m_pressed.reset();
				return false;
			}
			if (h->has(WfAcceptsFocus)) {
				focus = h;
			}
			m_pressed = eng::Ref<Widget>(h);
			return event_widget(*h, ev);
		}
		case UiEventKind::MouseUp: {
			Widget* t = m_pressed.get();
			m_pressed.reset();
			return t != nullptr ? event_widget(*t, ev) : false;
		}
		case UiEventKind::KeyDown:
			return focus != nullptr ? event_widget(*focus, ev) : false;
		case UiEventKind::None:
		case UiEventKind::MouseMove:
		case UiEventKind::KeyUp:
		case UiEventKind::JoyButton:
		case UiEventKind::Tick:
			return false;
		}
		return false;
	}

private:
	eng::Ref<Widget> m_pressed {};
};

} // namespace eng::ui
