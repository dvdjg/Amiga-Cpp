// ============================================================================
// Test HOST-312: GUI — lista con seleccion y desplazamiento (ListView).
// ============================================================================
//
// Valida `eng/ui/list.hpp`: filas visibles, `top` acotado, `ensure_visible` al seleccionar,
// teclado (flechas, Shift+pagina, Home/End), click -> fila y callback; mas un smoke test de
// dibujo (fila seleccionada con `fill_active`). Ver ROADMAP_GUI.md (widgets de scroll) y
// GUI_LIBRARY.md §11.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/312_ui_listview

#include <cstdio>

#include <eng/core/types/box.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/ui/painter.hpp>
#include <eng/ui/theme.hpp>
#include <eng/ui/widgets.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

constexpr eng::u16 kSW = 64u;
constexpr eng::u16 kSH = 32u;
constexpr eng::u16 kRow = static_cast<eng::u16>(((kSW / 8u) + 3u) & ~3u); // 8
constexpr eng::u32 kPlaneStride = static_cast<eng::u32>(kRow) * kSH;     // 256
constexpr eng::u8 kPlanes = 4u;

eng::u8 pixel_at(const eng::u8* base, eng::s16 x, eng::s16 y) {
	eng::u8 c = 0u;
	for (eng::u8 p = 0u; p < kPlanes; ++p) {
		const eng::u16* w = reinterpret_cast<const eng::u16*>(
			base + p * kPlaneStride + static_cast<eng::u32>(y) * kRow +
			static_cast<eng::u32>(x / 16) * 2u);
		const eng::u8 bit = static_cast<eng::u8>((*w >> (15u - (x & 15))) & 1u);
		c = static_cast<eng::u8>(c | static_cast<eng::u8>(bit << p));
	}
	return c;
}

eng::ui::UiEvent mouse(eng::ui::UiEventKind k, eng::s16 x, eng::s16 y) {
	eng::ui::UiEvent e {};
	e.kind = k;
	e.x = x;
	e.y = y;
	return e;
}

eng::ui::UiEvent key(eng::u16 k, bool shift) {
	eng::ui::UiEvent e {};
	e.kind = eng::ui::UiEventKind::KeyDown;
	e.key = k;
	e.shift = shift;
	return e;
}

int picked = 0;
void on_pick(void*) { ++picked; }

} // namespace

int main() {
	const char* items[] = {"one", "two", "thr", "fou", "fiv"};

	// --- Cobertura basica --------------------------------------------------
	eng::s16 sel = 0;
	eng::ui::ListView lv;
	lv.bounds = eng::ui::Rect {0, 0, 40u, 30u}; // 3 filas de 10 px
	lv.items = items;
	lv.count = 5u;
	lv.selected = &sel;
	lv.on_select = on_pick;
	check(lv.visible_rows() == 3, "filas visibles = alto / item_h");

	lv.select(4);
	check(sel == 4, "select fija el indice");
	check(lv.top == 2, "ensure_visible baja top (4 - 3 + 1)");
	lv.select(0);
	check(lv.top == 0, "ensure_visible sube top");
	lv.select(-1);
	check(sel == 0, "select(-1) se ignora");
	lv.select(99);
	check(sel == 0, "select fuera de rango se ignora");

	// --- Teclado -----------------------------------------------------------
	lv.set_flag(eng::ui::WfFocused);
	picked = 0;
	eng::ui::event_list(lv, key(eng::ui::kKeyDown, false));
	check(sel == 1, "flecha abajo avanza");
	eng::ui::event_list(lv, key(eng::ui::kKeyUp, false));
	check(sel == 0, "flecha arriba retrocede");
	eng::ui::event_list(lv, key(eng::ui::kKeyDown, true));
	check(sel == 3, "Shift+abajo = pagina (3)");
	eng::ui::event_list(lv, key(eng::ui::kKeyEnd, false));
	check(sel == 4 && lv.top == 2, "End -> ultima, visible");
	eng::ui::event_list(lv, key(eng::ui::kKeyHome, false));
	check(sel == 0 && lv.top == 0, "Home -> primera, top 0");
	check(picked == 5, "callback en cada cambio");
	check(!eng::ui::event_list(lv, key(eng::ui::kKeyLeft, false)), "Left no aplica");

	// --- Click -------------------------------------------------------------
	lv.top = 1; // desplazado: la fila 0 queda arriba fuera
	eng::ui::event_list(lv, mouse(eng::ui::UiEventKind::MouseDown, 10, 5));
	check(sel == 1, "click selecciona fila visible (top + 0)");
	check(lv.has(eng::ui::WfPressed), "MouseDown deja pulsada");
	eng::ui::event_list(lv, mouse(eng::ui::UiEventKind::MouseUp, 10, 5));
	check(!lv.has(eng::ui::WfPressed), "MouseUp libera");

	// --- Lista mas corta que el viewport ----------------------------------
	const char* few[] = {"a", "b"};
	eng::s16 fsel = -1;
	eng::ui::ListView fl;
	fl.bounds = eng::ui::Rect {0, 0, 20u, 30u};
	fl.items = few;
	fl.count = 2u;
	fl.selected = &fsel;
	fl.top = 9;
	fl.clamp_top();
	check(fl.top == 0, "top acotado si hay menos filas que el alto");

	// --- Smoketest de dibujo ----------------------------------------------
	alignas(2) eng::u8 mem[kPlaneStride * kPlanes] {};
	eng::field::ContiguousPlayfield pf {};
	check(pf.bind_raw(mem, sizeof(mem), kSW, kSH, kPlanes), "bind_raw pantalla");
	eng::field::Surface screen {pf, eng::field::SurfaceRect {0, 0, kSW, kSH}};
	eng::ui::UiTheme th {};
	th.edit_bg = 0u;
	th.fill_active = 1u;
	th.shine = 2u;
	th.shadow = 3u;
	th.text = 3u;
	eng::ui::UiPainter painter {screen, {}, th};
	lv.select(2);
	lv.top = 0;
	eng::ui::draw_list(lv, painter);
	// Fila 2 resaltada: rect x[1,39), y[20,30); x=32,y=24 fuera del texto ("thr").
	check(pixel_at(mem, 32, 24) == 1u, "fila seleccionada con fill_active");
	// Fila 0 no resaltada: interior = edit_bg.
	check(pixel_at(mem, 32, 3) == 0u, "fila no seleccionada con edit_bg");

	if (failures == 0) {
		std::printf("OK: lista con seleccion y desplazamiento validada.\n");
	} else {
		std::printf("FALLOS: %d\n", failures);
	}
	return failures == 0 ? 0 : 1;
}
