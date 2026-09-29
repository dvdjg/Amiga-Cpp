// ============================================================================
// Test HOST-314: GUI — layout adaptable (grid, flow/wrap, fit) y texto ajustado.
// ============================================================================
//
// Valida las extensiones de `eng/ui/layout.hpp` (grid, flow con wrap, column-fill, fit al
// contenido, centrado) y de `eng/ui/text.hpp` (`text_wrap_lines`/`text_wrapped_width`/
// `draw_text_wrapped`) + `Label` con `wrap`. Ver GUI_LIBRARY.md 12 y ROADMAP_GUI.md.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/314_ui_layout_adaptive

#include <cstdio>

#include <eng/core/types/box.hpp>
#include <eng/ui/layout.hpp>
#include <eng/ui/widgets.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

} // namespace

int main() {
	// --- Rejilla 2x2 ---
	eng::ui::Panel g {};
	g.bounds = eng::ui::Rect {0, 0, 100, 100};
	eng::ui::Button b0 {}, b1 {}, b2 {}, b3 {};
	b0.bounds = eng::ui::Rect {0, 0, 20, 10};
	b1.bounds = eng::ui::Rect {0, 0, 20, 10};
	b2.bounds = eng::ui::Rect {0, 0, 24, 12}; // más alto: fija row_h de la fila 0
	b3.bounds = eng::ui::Rect {0, 0, 20, 10};
	g.add_child(&b3); g.add_child(&b2); g.add_child(&b1); g.add_child(&b0);
	eng::ui::layout_grid(g, 2u, 4u, 6u);
	// Orden de creación = b3,b2,b1,b0. Fila 0: b3(0,0) b2(24,0), row_h=12.
	// Fila 1 (y = 0 + 12 + 6 = 18): b1(0,18) b0(24,18).
	check(b3.bounds.x == 0 && b3.bounds.y == 0, "grid b3 en (0,0)");
	check(b2.bounds.x == 24 && b2.bounds.y == 0, "grid b2 en (24,0)");
	check(b1.bounds.x == 0 && b1.bounds.y == 18, "grid b1 en (0,18)");
	check(b0.bounds.x == 24 && b0.bounds.y == 18, "grid b0 en (24,18)");

	// --- Flow con wrap: ancho 50, celdas de 20, gap 4 -> 2 por fila ---
	eng::ui::Panel f {};
	f.bounds = eng::ui::Rect {0, 0, 50, 100};
	eng::ui::Button c0 {}, c1 {}, c2 {};
	c0.bounds = eng::ui::Rect {0, 0, 20, 10};
	c1.bounds = eng::ui::Rect {0, 0, 20, 10};
	c2.bounds = eng::ui::Rect {0, 0, 20, 10};
	f.add_child(&c2); f.add_child(&c1); f.add_child(&c0);
	eng::ui::layout_flow(f, 4u, 6u);
	// Creación = c2,c1,c0. Ancho 50, celdas 20+4 => caben 2 por fila.
	check(c2.bounds.x == 0 && c2.bounds.y == 0, "flow c2 (0,0)");
	check(c1.bounds.x == 24 && c1.bounds.y == 0, "flow c1 (24,0)");
	check(c0.bounds.x == 0 && c0.bounds.y == 16, "flow c0 wrap a (0,16)");

	// --- Column-fill: expande a lo ancho ---
	eng::ui::Panel col {};
	col.bounds = eng::ui::Rect {0, 0, 80, 100};
	eng::ui::EditBox e0 {}, e1 {};
	e0.bounds = eng::ui::Rect {0, 0, 10, 12};
	e1.bounds = eng::ui::Rect {0, 0, 10, 12};
	col.add_child(&e1); col.add_child(&e0);
	eng::ui::layout_column_fill(col, 4u);
	// Creación = e1, e0.
	check(e1.bounds.w == 80 && e0.bounds.w == 80, "column_fill expande a 80");
	check(e1.bounds.y == 0 && e0.bounds.y == 16, "column_fill apila con gap 4");

	// --- fit al contenido (label adaptable) ---
	eng::ui::Panel fit {};
	fit.bounds = eng::ui::Rect {0, 0, 0, 0};
	eng::ui::Label l0 {}, l1 {};
	l0.text = "AAAA";   // 4 * 8 = 32
	l1.text = "BBBBBB"; // 6 * 8 = 48
	fit.add_child(&l1); fit.add_child(&l0);
	eng::ui::UiTheme th {};
	eng::ui::Rect sz = eng::ui::layout_fit_children(fit, [&](eng::ui::Widget& w) {
		return eng::ui::measure(w, th);
	}, 2u, true);
	check(sz.w == 48 && sz.h == 8u + 2u + 8u, "fit columna: ancho 48, alto 18");
	check(fit.bounds.h == 18, "fit ajusta el alto del padre");

	// --- Wrapping de texto (medida y nº de líneas) ---
	// "AA BB CC" con 4 columnas (32 px), por palabra: "AA" / "BB CC"? La medición es determinista.
	check(eng::ui::text_wrap_lines("AAAA", 32u, eng::ui::WrapMode::Word) == 1u,
	      "wrap: 4 chars en 32 px = 1 linea");
	check(eng::ui::text_wrap_lines("AAAAA", 32u, eng::ui::WrapMode::Char) == 2u,
	      "wrap char: 5 chars en 4 cols = 2 lineas");
	const eng::u16 wl = eng::ui::text_wrap_lines("AA BB CC", 32u, eng::ui::WrapMode::Word);
	check(wl >= 2u, "wrap word: varias lineas");
	check(eng::ui::text_wrapped_width("AAAAAAAAAA", 32u, eng::ui::WrapMode::Word) == 32u,
	      "ancho ajustado <= max_w");
	check(eng::ui::text_wrapped_width("AAAA", 32u, eng::ui::WrapMode::Word) == 32u,
	      "ancho sin ajuste (cabe)");

	// --- Label con wrap: measure usa el ancho de ajuste ---
	eng::ui::Label lw {};
	lw.text = "AAAAA"; // 40 px
	lw.wrap = eng::ui::WrapMode::Char;
	lw.wrap_w = 32u;
	eng::ui::Rect m = eng::ui::measure(lw, th);
	check(m.w == 32u && m.h == 16u, "label con wrap: 32 px x 2 lineas");

	if (failures == 0) {
		std::printf("OK: layout adaptable (grid/flow/fit) y texto ajustado validados.\n");
	} else {
		std::printf("FALLOS: %d\n", failures);
	}
	return failures == 0 ? 0 : 1;
}
