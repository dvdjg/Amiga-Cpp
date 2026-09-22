#pragma once

/// \file layout.hpp
/// **Layout de UI** (`eng::ui`, G5): sin motor de *constraints*, para A500 basta con pila
/// vertical/horizontal (con `gap`) y **anclaje** a un borde del padre. Los hijos se colocan en
/// **orden de creación**. Ver `docs/engine/architecture/GUI_LIBRARY.md` §12.
///
/// Los hijos deben llevar ya su tamaño (`bounds.w/h`), p. ej. desde `measure(w, theme)`.

#include <eng/core/types.hpp>
#include <eng/ui/widget.hpp>

namespace eng::ui {

/// Nº máximo de hijos que el layout coloca (cota de compilación, sin heap).
inline constexpr eng::u8 kMaxLayoutChildren = 32u;

/// Coloca los hijos de `g` en pila **vertical** desde `g.bounds` (esquina superior izquierda),
/// separados `gap` px, en orden de creación.
inline void layout_stack_v(Widget& g, eng::u8 gap) noexcept {
	Widget* kids[kMaxLayoutChildren];
	const eng::u8 n = children_creation_order<kMaxLayoutChildren>(g, kids);
	eng::s16 y = g.bounds.y;
	for (eng::u8 i = 0u; i < n; ++i) {
		Widget& c = *kids[i];
		c.bounds.x = g.bounds.x;
		c.bounds.y = y;
		y = static_cast<eng::s16>(y + c.bounds.h + gap);
		c.mark_dirty();
	}
}

/// Coloca los hijos de `g` en pila **horizontal** desde `g.bounds`, separados `gap` px.
inline void layout_stack_h(Widget& g, eng::u8 gap) noexcept {
	Widget* kids[kMaxLayoutChildren];
	const eng::u8 n = children_creation_order<kMaxLayoutChildren>(g, kids);
	eng::s16 x = g.bounds.x;
	for (eng::u8 i = 0u; i < n; ++i) {
		Widget& c = *kids[i];
		c.bounds.x = x;
		c.bounds.y = g.bounds.y;
		x = static_cast<eng::s16>(x + c.bounds.w + gap);
		c.mark_dirty();
	}
}

/// Anclaje de un widget respecto a los `bounds` de su padre (o a los suyos si no tiene).
enum class Anchor : eng::u8 {
	TopLeft,
	Top,
	TopRight,
	Left,
	Center,
	Right,
	BottomLeft,
	Bottom,
	BottomRight,
};

/// Coloca `w` anclado a `a` dentro de su padre, con desplazamiento `(dx, dy)`.
inline void anchor(Widget& w, Anchor a, eng::s16 dx = 0, eng::s16 dy = 0) noexcept {
	const Rect p = (w.parent != nullptr) ? w.parent->bounds : w.bounds;
	const eng::u16 cw = w.bounds.w;
	const eng::u16 ch = w.bounds.h;
	eng::s16 x = p.x;
	eng::s16 y = p.y;
	switch (a) {
	case Anchor::TopLeft:
		break;
	case Anchor::Top:
		x = static_cast<eng::s16>(p.x + (p.w > cw ? (p.w - cw) / 2u : 0u));
		break;
	case Anchor::TopRight:
		x = static_cast<eng::s16>(p.x + p.w - cw);
		break;
	case Anchor::Left:
		y = static_cast<eng::s16>(p.y + (p.h > ch ? (p.h - ch) / 2u : 0u));
		break;
	case Anchor::Center:
		x = static_cast<eng::s16>(p.x + (p.w > cw ? (p.w - cw) / 2u : 0u));
		y = static_cast<eng::s16>(p.y + (p.h > ch ? (p.h - ch) / 2u : 0u));
		break;
	case Anchor::Right:
		x = static_cast<eng::s16>(p.x + p.w - cw);
		y = static_cast<eng::s16>(p.y + (p.h > ch ? (p.h - ch) / 2u : 0u));
		break;
	case Anchor::BottomLeft:
		y = static_cast<eng::s16>(p.y + p.h - ch);
		break;
	case Anchor::Bottom:
		x = static_cast<eng::s16>(p.x + (p.w > cw ? (p.w - cw) / 2u : 0u));
		y = static_cast<eng::s16>(p.y + p.h - ch);
		break;
	case Anchor::BottomRight:
		x = static_cast<eng::s16>(p.x + p.w - cw);
		y = static_cast<eng::s16>(p.y + p.h - ch);
		break;
	}
	w.bounds.x = static_cast<eng::s16>(x + dx);
	w.bounds.y = static_cast<eng::s16>(y + dy);
	w.mark_dirty();
}

} // namespace eng::ui
