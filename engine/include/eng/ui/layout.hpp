#pragma once

/// \file layout.hpp
/// **Layout de UI** (`eng::ui`, G5): sin motor de *constraints*, para A500 basta con pila
/// vertical/horizontal (con `gap`) y **anclaje** a un borde del padre. Los hijos se colocan en
/// **orden de creación**. Ver `docs/engine/architecture/GUI_LIBRARY.md` §12.
///
/// Los hijos deben llevar ya su tamaño (`bounds.w/h`), p. ej. desde `measure(w, theme)`.

#include <eng/core/types/types.hpp>
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

// ---------------------------------------------------------------------------
// Layouts sin constraints: rejilla, flujo con *wrap* y ajuste al contenido.
// Todos usan el **orden de creación** de los hijos y capacidad fija (sin heap).
// ---------------------------------------------------------------------------

/// Coloca los hijos en **rejilla** de `cols` columnas desde `g.bounds`, con `gap_x`/`gap_y`.
/// El ancho/alto de celda lo fijan los hijos (cada uno mantiene su tamaño); las celdas se
/// alinean por filas usando `g.bounds.x/y` como origen. `cols==0` se trata como 1.
inline void layout_grid(Widget& g, eng::u8 cols, eng::u8 gap_x, eng::u8 gap_y) noexcept {
	if (cols == 0u) {
		cols = 1u;
	}
	Widget* kids[kMaxLayoutChildren];
	const eng::u8 n = children_creation_order<kMaxLayoutChildren>(g, kids);
	eng::s16 x = g.bounds.x;
	eng::s16 y = g.bounds.y;
	eng::s16 row_h = 0;
	eng::s16 row_x0 = g.bounds.x;
	for (eng::u8 i = 0u; i < n; ++i) {
		Widget& c = *kids[i];
		const eng::u8 col = static_cast<eng::u8>(i % cols);
		if (col == 0u) {
			x = row_x0;
			if (i != 0u) {
				y = static_cast<eng::s16>(y + row_h + gap_y);
			}
			row_h = 0;
		}
		c.bounds.x = x;
		c.bounds.y = y;
		x = static_cast<eng::s16>(x + c.bounds.w + gap_x);
		if (static_cast<eng::s16>(c.bounds.h) > row_h) {
			row_h = static_cast<eng::s16>(c.bounds.h);
		}
		c.mark_dirty();
	}
}

/// Coloca los hijos en **flujo** horizontal con *wrap* al llegar a `g.bounds.w`. `gap_x` es la
/// separación horizontal y `gap_y` la vertical entre líneas. Los hijos mantienen su tamaño; el
/// `wrap` usa el ancho del padre. Es el layout natural para etiquetas/botones en una columna.
inline void layout_flow(Widget& g, eng::u8 gap_x, eng::u8 gap_y) noexcept {
	Widget* kids[kMaxLayoutChildren];
	const eng::u8 n = children_creation_order<kMaxLayoutChildren>(g, kids);
	const eng::s16 max_x = static_cast<eng::s16>(g.bounds.x + g.bounds.w);
	eng::s16 x = g.bounds.x;
	eng::s16 y = g.bounds.y;
	eng::s16 row_h = 0;
	for (eng::u8 i = 0u; i < n; ++i) {
		Widget& c = *kids[i];
		if (x != g.bounds.x &&
		    static_cast<eng::s16>(x + c.bounds.w) > max_x) {
			x = g.bounds.x;
			y = static_cast<eng::s16>(y + row_h + gap_y);
			row_h = 0;
		}
		c.bounds.x = x;
		c.bounds.y = y;
		x = static_cast<eng::s16>(x + c.bounds.w + gap_x);
		if (static_cast<eng::s16>(c.bounds.h) > row_h) {
			row_h = static_cast<eng::s16>(c.bounds.h);
		}
		c.mark_dirty();
	}
}

/// Coloca los hijos en **columna**, y **expande** cada uno a lo ancho del padre
/// (`g.bounds.w`) manteniendo su alto. Es el layout habitual de un formulario: los campos
/// ocupan el ancho disponible. `gap` separa verticalmente.
inline void layout_column_fill(Widget& g, eng::u8 gap) noexcept {
	Widget* kids[kMaxLayoutChildren];
	const eng::u8 n = children_creation_order<kMaxLayoutChildren>(g, kids);
	eng::s16 y = g.bounds.y;
	for (eng::u8 i = 0u; i < n; ++i) {
		Widget& c = *kids[i];
		c.bounds.x = g.bounds.x;
		c.bounds.y = y;
		c.bounds.w = g.bounds.w;
		y = static_cast<eng::s16>(y + c.bounds.h + gap);
		c.mark_dirty();
	}
}

/// Ajusta el **tamaño de cada hijo al de su contenido** (alto, y opcionalmente ancho) según lo
/// que devuelva `measure`. Es el layout adaptable del requisito «label que se adapta al texto»:
/// el padre se redimensiona a la suma de sus hijos (columna) o al mayor (según `vertical`).
///
/// El llamador pasa una función `measure(Widget&)` (p. ej. `eng::ui::measure`) que devuelve el
/// tamaño preferido; el layout no conoce los tipos. Devuelve el tamaño total ocupado.
template <class MeasureFn>
Rect layout_fit_children(Widget& g, MeasureFn&& measure, eng::u8 gap, bool vertical,
			 bool expand_width = false) noexcept {
	Widget* kids[kMaxLayoutChildren];
	const eng::u8 n = children_creation_order<kMaxLayoutChildren>(g, kids);
	eng::u16 total_w = 0u;
	eng::u16 total_h = 0u;
	for (eng::u8 i = 0u; i < n; ++i) {
		Widget& c = *kids[i];
		const Rect pref = measure(c);
		c.bounds.w = pref.w;
		c.bounds.h = pref.h;
		c.mark_dirty();
		if (vertical) {
			total_h = static_cast<eng::u16>(total_h + pref.h + (i != 0u ? gap : 0u));
			if (pref.w > total_w) {
				total_w = pref.w;
			}
		} else {
			total_w = static_cast<eng::u16>(total_w + pref.w + (i != 0u ? gap : 0u));
			if (pref.h > total_h) {
				total_h = pref.h;
			}
		}
	}
	if (expand_width) {
		g.bounds.w = total_w;
	} else {
		g.bounds.h = total_h;
	}
	g.mark_dirty();
	return Rect {g.bounds.x, g.bounds.y, total_w, total_h};
}

/// Coloca los hijos **centrados** en el padre (uno debajo de otro, en columna), útil para
/// diálogos/mensajes. `gap` separa verticalmente; el bloque se centra como conjunto.
inline void layout_center_column(Widget& g, eng::u8 gap) noexcept {
	Widget* kids[kMaxLayoutChildren];
	const eng::u8 n = children_creation_order<kMaxLayoutChildren>(g, kids);
	eng::u16 total_h = 0u;
	for (eng::u8 i = 0u; i < n; ++i) {
		total_h = static_cast<eng::u16>(total_h + kids[i]->bounds.h + (i != 0u ? gap : 0u));
	}
	eng::s16 y = static_cast<eng::s16>(g.bounds.y + (g.bounds.h > total_h ? (g.bounds.h - total_h) / 2u : 0u));
	for (eng::u8 i = 0u; i < n; ++i) {
		Widget& c = *kids[i];
		c.bounds.x = static_cast<eng::s16>(g.bounds.x + (g.bounds.w > c.bounds.w ? (g.bounds.w - c.bounds.w) / 2u : 0u));
		c.bounds.y = y;
		y = static_cast<eng::s16>(y + c.bounds.h + gap);
		c.mark_dirty();
	}
}

} // namespace eng::ui
