#pragma once

/// \file widget.hpp
/// **Árbol de widgets** de `eng::ui`: datos planos + árbol intrusivo (padre/hijo/siguiente),
/// **sin heap y sin `virtual`**. El despacho por tipo vive fuera (`switch` exhaustivo en
/// `widgets.hpp`), de modo que un `Widget` no arrastra vtable. Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §8.

#include <eng/core/types/box.hpp>
#include <eng/core/types/types.hpp>
#include <eng/ui/theme.hpp>

namespace eng::ui {

/// Tipo de widget: discriminante para el despacho con `switch` exhaustivo.
enum class WidgetType : eng::u8 {
	Panel,
	Label,
	Button,
	Check,
	Radio,
	Edit,
	Slider,
	List,
	Window,
};

/// Flags de widget (bitmask).
enum WidgetFlags : eng::u16 {
	WfVisible = 1u << 0,
	WfEnabled = 1u << 1,
	WfDirty = 1u << 2,
	WfFocused = 1u << 3,
	WfPressed = 1u << 4,
	WfModal = 1u << 5,
	WfAcceptsFocus = 1u << 6,
	WfNoInput = 1u << 7, ///< no capta input (p. ej. un Toast): el hit-test lo salta
};

/// Nodo del árbol de widgets. Los datos concretos de cada tipo van en los structs derivados
/// (`Panel`, `Label`, …), no en una unión opaca. Los hijos se insertan al frente: el orden de la
/// lista de hermanos es el orden Z (el primero = el más al frente).
struct Widget {
	WidgetType type = WidgetType::Panel;
	Rect bounds {};
	eng::u16 flags = static_cast<eng::u16>(WfVisible | WfEnabled | WfDirty);

	Widget* parent = nullptr;
	Widget* first_child = nullptr;
	Widget* next = nullptr; ///< siguiente hermano (el primero = más al frente)

	[[nodiscard]] constexpr bool has(eng::u16 f) const { return (flags & f) != 0u; }
	void set_flag(eng::u16 f) { flags = static_cast<eng::u16>(flags | f); }
	void clear_flag(eng::u16 f) { flags = static_cast<eng::u16>(flags & ~f); }
	void mark_dirty() { set_flag(WfDirty); }

	/// Añade `c` al frente de los hijos (Z: el último añadido queda más al frente) y marca el
	/// contenedor sucio.
	void add_child(Widget* c) noexcept {
		if (c == nullptr) {
			return;
		}
		c->parent = this;
		c->next = first_child;
		first_child = c;
		mark_dirty();
	}
};

/// Sube `w` al frente de sus hermanos (orden Z: el primero de la lista es el más al frente).
inline void raise(Widget& w) noexcept {
	Widget* p = w.parent;
	if (p == nullptr) {
		return;
	}
	if (p->first_child == &w) {
		return; // ya está al frente
	}
	for (Widget* c = p->first_child; c != nullptr; c = c->next) {
		if (c->next == &w) {
			c->next = w.next;
			break;
		}
	}
	w.next = p->first_child;
	p->first_child = &w;
	p->mark_dirty();
}

/// Marca sucio `w` y todos sus ancestros: un cambio en un hijo obliga a repintar la rama.
inline void mark_dirty_up(Widget& w) noexcept {
	for (Widget* n = &w; n != nullptr; n = n->parent) {
		n->mark_dirty();
	}
}

/// Marca sucio `w` y todo su subárbol (p. ej. al cambiar de tema).
inline void mark_all_dirty(Widget& w) noexcept {
	w.mark_dirty();
	for (Widget* c = w.first_child; c != nullptr; c = c->next) {
		mark_all_dirty(*c);
	}
}

/// Llena `out` (hasta `Max`) con los hijos de `parent` en **orden de creación** (el más antiguo
/// primero). La lista interna es Z (el más nuevo al frente), así que se invierte in situ.
template <eng::u8 Max>
[[nodiscard]] inline eng::u8 children_creation_order(const Widget& parent,
						     Widget** out) noexcept {
	eng::u8 n = 0u;
	for (Widget* c = parent.first_child; c != nullptr && n < Max; c = c->next) {
		out[n++] = c;
	}
	for (eng::u8 i = 0u; i < static_cast<eng::u8>(n / 2u); ++i) {
		Widget* t = out[i];
		out[i] = out[static_cast<eng::u8>(n - 1u - i)];
		out[static_cast<eng::u8>(n - 1u - i)] = t;
	}
	return n;
}

} // namespace eng::ui
