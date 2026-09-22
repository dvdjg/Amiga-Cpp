#pragma once

/// \file widget.hpp
/// **Árbol de widgets** de `eng::ui`: datos planos + árbol intrusivo (padre/hijo/siguiente),
/// **sin heap y sin `virtual`**. El despacho por tipo vive fuera (`switch` exhaustivo en
/// `widgets.hpp`), de modo que un `Widget` no arrastra vtable. Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §8.

#include <eng/core/box.hpp>
#include <eng/core/types.hpp>
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

/// Marca sucio `w` y todos sus ancestros: un cambio en un hijo obliga a repintar la rama.
inline void mark_dirty_up(Widget& w) noexcept {
	for (Widget* n = &w; n != nullptr; n = n->parent) {
		n->mark_dirty();
	}
}

} // namespace eng::ui
