#pragma once

/// \file context.hpp
/// **`eng::ui::UiContext`**: hit-test, foco y despacho de eventos sobre el árbol de widgets.
/// No lee hardware: consume `UiEvent` (del puente `eng::os::Msg` o de entrada síncrona). Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §10 y §13.
///
/// Reglas de entrada:
/// - hit-test de **delante hacia atrás**; con un **modal** (`WfModal`) abierto, solo su subárbol.
/// - el ratón engancha el widget en `MouseDown` y le entrega el `MouseUp`.
/// - `Tab`/`Shift+Tab` cicla el foco (dentro del modal si lo hay).
/// - `Esc` cierra el popup/diálogo superior; pulsar fuera cierra el popup.
/// - los `Toast` (`WfNoInput`) no captan input y expiran por `Tick`.

#include <eng/core/ptr.hpp>
#include <eng/core/types.hpp>
#include <eng/ui/event.hpp>
#include <eng/ui/keymap.hpp>
#include <eng/ui/keys.hpp>
#include <eng/ui/widget.hpp>
#include <eng/ui/widgets.hpp>
#include <eng/ui/window.hpp>

namespace eng::ui {

class UiContext {
public:
	Widget* root = nullptr;
	Widget* focus = nullptr;
	/// Distribución nacional con la que se traduce el rawkey (ver `keymap.hpp`). La fija la
	/// aplicación al arrancar (según el país del sistema o su configuración), no va fija en el código.
	KeyboardLayout layout = KeyboardLayout::Us;

	void set_root(Widget* r) noexcept { root = r; }

	/// Frente de la pila de modales (o `nullptr`).
	[[nodiscard]] Widget* top_modal() const noexcept { return find_modal(root); }

	/// Devuelve el widget más al frente que contiene `(px, py)` (o `nullptr`). Salta los `Toast`.
	[[nodiscard]] Widget* hit_test(Widget* node, eng::s16 px, eng::s16 py) const noexcept {
		if (node == nullptr || !node->has(WfVisible) || node->has(WfNoInput)) {
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

	/// Da el foco a `n`: quita `WfFocused` del anterior y lo pone en `n` (marca ambos sucios).
	void set_focus(Widget* n) noexcept {
		if (focus == n) {
			return;
		}
		if (focus != nullptr) {
			focus->clear_flag(WfFocused);
			focus->mark_dirty();
		}
		focus = n;
		if (focus != nullptr) {
			focus->set_flag(WfFocused);
			focus->mark_dirty();
		}
	}

	/// Cicla el foco al siguiente (o anterior con `shift`) widget `WfAcceptsFocus` visible y
	/// habilitado, en orden de árbol, con vuelta. Con un modal abierto, solo dentro de él.
	void focus_next(bool shift) noexcept {
		Widget* basis = top_modal();
		if (basis == nullptr) {
			basis = root;
		}
		if (basis == nullptr) {
			return;
		}
		FocusWalk w {};
		w.cur = focus;
		walk_focus(basis, w);
		Widget* target = nullptr;
		if (focus == nullptr) {
			target = w.first;
		} else if (shift) {
			target = (w.prev != nullptr) ? w.prev : w.last;
		} else {
			target = (w.next != nullptr) ? w.next : w.first;
		}
		set_focus(target);
	}

	/// Despacha `ev`. Devuelve `true` si algún widget lo consumió.
	bool dispatch(const UiEvent& ev) {
		if (root == nullptr) {
			return false;
		}
		switch (ev.kind) {
		case UiEventKind::MouseDown: {
			// Un click fuera del popup abierto lo cierra y no pasa al fondo.
			Window* pop = find_top_kind(root, WindowKind::Popup);
			if (pop != nullptr && !pop->bounds.contains(ev.x, ev.y)) {
				if (pop->close_on_outside) {
					pop->clear_flag(WfVisible);
					pop->mark_dirty();
				}
				return true;
			}
			Widget* basis = top_modal();
			if (basis == nullptr) {
				basis = root;
			}
			Widget* h = hit_test(basis, ev.x, ev.y);
			if (h == nullptr) {
				m_pressed.reset();
				return false;
			}
			if (h->has(WfAcceptsFocus)) {
				set_focus(h);
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
			if (ev.key == kKeyTab) {
				focus_next(ev.shift);
				return true;
			}
			if (ev.key == kKeyEsc) {
				Window* top = find_top_kind(root, WindowKind::Dialog);
				if (top == nullptr) {
					top = find_top_kind(root, WindowKind::Popup);
				}
				if (top != nullptr) {
					top->clear_flag(WfVisible);
					top->mark_dirty();
					return true;
				}
			}
			return focus != nullptr ? event_widget(*focus, ev) : false;
		case UiEventKind::Tick:
			tick_toasts(root);
			return false;
		case UiEventKind::None:
		case UiEventKind::MouseMove:
		case UiEventKind::KeyUp:
		case UiEventKind::JoyButton:
			return false;
		}
		return false;
	}

private:
	struct FocusWalk {
		Widget* first = nullptr;
		Widget* last = nullptr;
		Widget* prev = nullptr;
		Widget* next = nullptr;
		Widget* cur = nullptr;
		bool seen_cur = false;
	};

	/// Recorre en orden de árbol anotando el primero, el último, el anterior a `cur` y el
	/// siguiente a `cur` entre los widgets que aceptan foco.
	static void walk_focus(Widget* n, FocusWalk& w) noexcept {
		if (n == nullptr || !n->has(WfVisible)) {
			return;
		}
		if (n->has(WfAcceptsFocus) && n->has(WfEnabled)) {
			if (w.first == nullptr) {
				w.first = n;
			}
			if (w.seen_cur) {
				if (w.next == nullptr) {
					w.next = n;
				}
			} else if (n == w.cur) {
				w.seen_cur = true;
				w.prev = w.last;
			}
			w.last = n;
		}
		for (Widget* c = n->first_child; c != nullptr; c = c->next) {
			walk_focus(c, w);
		}
	}

	/// Primer widget visible con `WfModal` (el más al frente).
	static Widget* find_modal(Widget* n) noexcept {
		if (n == nullptr || !n->has(WfVisible)) {
			return nullptr;
		}
		if (n->has(WfModal)) {
			return n;
		}
		for (Widget* c = n->first_child; c != nullptr; c = c->next) {
			if (Widget* m = find_modal(c); m != nullptr) {
				return m;
			}
		}
		return nullptr;
	}

	/// Ventana visible más al frente de la clase `k`.
	static Window* find_top_kind(Widget* n, WindowKind k) noexcept {
		if (n == nullptr || !n->has(WfVisible)) {
			return nullptr;
		}
		if (n->type == WidgetType::Window) {
			auto& w = static_cast<Window&>(*n);
			if (w.kind == k) {
				return &w;
			}
		}
		for (Widget* c = n->first_child; c != nullptr; c = c->next) {
			if (Window* r = find_top_kind(c, k); r != nullptr) {
				return r;
			}
		}
		return nullptr;
	}

	/// Decrementa el TTL de los `Toast` visibles y los oculta al llegar a 0.
	static void tick_toasts(Widget* n) noexcept {
		if (n == nullptr || !n->has(WfVisible)) {
			return;
		}
		if (n->type == WidgetType::Window) {
			auto& w = static_cast<Window&>(*n);
			if (w.kind == WindowKind::Toast && w.ttl > 0u) {
				--w.ttl;
				w.mark_dirty();
				if (w.ttl == 0u) {
					w.clear_flag(WfVisible);
				}
			}
		}
		for (Widget* c = n->first_child; c != nullptr; c = c->next) {
			tick_toasts(c);
		}
	}

	eng::Ref<Widget> m_pressed {};
};

} // namespace eng::ui
