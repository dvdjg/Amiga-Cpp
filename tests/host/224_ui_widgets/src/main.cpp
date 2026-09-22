// ============================================================================
// Test HOST-224: GUI G1 — arbol de widgets, dirty rects, medida y dibujo.
// ============================================================================
//
// Valida `eng/ui/widget.hpp` (arbol intrusivo), `dirty.hpp` (`DirtyList<Max>`) y `widgets.hpp`
// (`Panel`/`Label`, despacho por `switch` exhaustivo y `measure`). El dibujo se comprueba sobre
// un `ContiguousPlayfield` host leyendo el color de cada pixel (como HOST-223).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/224_ui_widgets

#include <cstdio>

#include <eng/core/box.hpp>
#include <eng/field/surface.hpp>
#include <eng/ui/dirty.hpp>
#include <eng/ui/widget.hpp>
#include <eng/ui/widgets.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

constexpr eng::u16 kW = 32u;
constexpr eng::u16 kH = 16u;
constexpr eng::u16 kRow = static_cast<eng::u16>(((kW / 8u) + 3u) & ~3u);
constexpr eng::u32 kPlaneStride = static_cast<eng::u32>(kRow) * kH;
constexpr eng::u8 kPlanes = 4u;
const eng::ui::Rect kFull {0, 0, kW, kH};

constexpr eng::ui::UiTheme kT {
	.shine = 1u, .shadow = 2u, .fill = 3u, .text = 5u,
	.panel_frame = eng::ui::FrameStyle::Raised,
};

const eng::u8* g_planes = nullptr;

eng::u8 pixel_at(eng::s16 x, eng::s16 y) {
	eng::u8 c = 0u;
	for (eng::u8 p = 0u; p < kPlanes; ++p) {
		const eng::u16* w = reinterpret_cast<const eng::u16*>(
			g_planes + p * kPlaneStride + static_cast<eng::u32>(y) * kRow +
			static_cast<eng::u32>(x / 16) * 2u);
		const eng::u8 bit = static_cast<eng::u8>((*w >> (15u - (x & 15))) & 1u);
		c = static_cast<eng::u8>(c | static_cast<eng::u8>(bit << p));
	}
	return c;
}

eng::u32 count_colored(eng::s16 x0, eng::s16 x1, eng::s16 y0, eng::s16 y1) {
	eng::u32 n = 0u;
	for (eng::s16 y = y0; y <= y1; ++y) {
		for (eng::s16 x = x0; x <= x1; ++x) {
			if (pixel_at(x, y) != 0u) {
				++n;
			}
		}
	}
	return n;
}

} // namespace

int main() {
	// --- Arbol intrusivo ---------------------------------------------------
	eng::ui::Panel root;
	eng::ui::Label a;
	eng::ui::Label b;
	root.add_child(&a);
	check(root.first_child == &a && a.parent == &root && a.next == nullptr,
	      "primer hijo enlazado");
	root.add_child(&b);
	check(root.first_child == &b && b.next == &a && a.next == nullptr,
	      "hijo nuevo al frente (orden Z)");

	root.clear_flag(eng::ui::WfDirty);
	a.clear_flag(eng::ui::WfDirty);
	eng::ui::mark_dirty_up(a);
	check(a.has(eng::ui::WfDirty) && root.has(eng::ui::WfDirty), "mark_dirty_up propaga");

	check(eng::ui::Panel {}.has(eng::ui::WfVisible), "widget visible por defecto");

	// --- DirtyList ---------------------------------------------------------
	eng::ui::DirtyList<4> d;
	d.add(eng::ui::Rect {0, 0, 10u, 10u});
	d.add(eng::ui::Rect {5, 5, 10u, 10u});
	check(d.count == 1u && d.rects[0].x == 0 && d.rects[0].y == 0 &&
		      d.rects[0].w == 15u && d.rects[0].h == 15u,
	      "fusión por solape");
	d.add(eng::ui::Rect {100, 100, 4u, 4u});
	check(d.count == 2u, "región disjunta se añade");
	d.add(eng::ui::Rect {0, 0, 0u, 0u});
	check(d.count == 2u, "rect vacío ignorado");
	d.clear();
	check(d.count == 0u, "clear");

	eng::ui::DirtyList<2> d2;
	d2.add(eng::ui::Rect {0, 0, 4u, 4u});
	d2.add(eng::ui::Rect {10, 10, 4u, 4u});
	d2.add(eng::ui::Rect {20, 20, 4u, 4u});
	check(d2.count == 1u && d2.rects[0].w == 320u && d2.rects[0].h == 256u,
	      "desborde -> repintado total");

	// --- measure -----------------------------------------------------------
	eng::ui::Label l;
	l.text = "ABC";
	check(eng::ui::measure(l).w == 24u && eng::ui::measure(l).h == 8u, "measure Label");
	eng::ui::Panel pn;
	pn.bounds = eng::ui::Rect {1, 2, 10u, 20u};
	check(eng::ui::measure(pn).w == 10u && eng::ui::measure(pn).h == 20u,
	      "measure Panel = bounds");

	// --- dibujo sobre Surface ---------------------------------------------
	alignas(2) eng::u8 planes[kPlaneStride * kPlanes] {};
	g_planes = planes;
	eng::field::ContiguousPlayfield pf {};
	check(pf.bind_raw(planes, sizeof(planes), kW, kH, kPlanes), "bind_raw del playfield");
	eng::field::Surface surf {pf, eng::field::SurfaceRect {0, 0, kW, kH}};
	eng::ui::UiPainter p {surf, nullptr, kT};
	const auto clear = [&]() { p.fill(kFull, 0u); };

	clear();
	eng::ui::Panel pnl;
	pnl.bounds = eng::ui::Rect {0, 0, 12u, 10u};
	eng::ui::draw_widget(pnl, p);
	check(pixel_at(6, 5) == 3u, "Panel dibuja el relleno del tema");
	check(pixel_at(6, 0) == 1u && pixel_at(6, 9) == 2u, "Panel dibuja el bisel");

	clear();
	eng::ui::Label lab;
	lab.bounds = eng::ui::Rect {4, 4, 0u, 0u};
	lab.text = "A";
	eng::ui::draw_widget(lab, p);
	check(count_colored(4, 11, 4, 11) > 0u, "Label dibuja el texto");

	clear();
	pnl.clear_flag(eng::ui::WfVisible);
	eng::ui::draw_widget(pnl, p);
	check(count_colored(0, kW - 1, 0, kH - 1) == 0u, "widget oculto no pinta");

	if (failures == 0) {
		std::printf("OK: GUI G1 (arbol, dirty, medida, Panel/Label) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
