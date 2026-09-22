// ============================================================================
// Test HOST-228: GUI G5 — layout (pila/anclaje) y cambio de tema.
// ============================================================================
//
// Valida `eng/ui/layout.hpp` (layout_stack_v/h, anchor) y el cambio de tema (recolorear),
// ademas de `measure` de un boton y `mark_all_dirty`. Ver ROADMAP_GUI.md (G5).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/228_ui_layout

#include <cstdio>

#include <eng/core/box.hpp>
#include <eng/field/surface.hpp>
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

constexpr eng::u16 kW = 32u;
constexpr eng::u16 kH = 16u;
constexpr eng::u16 kRow = static_cast<eng::u16>(((kW / 8u) + 3u) & ~3u);
constexpr eng::u32 kPlaneStride = static_cast<eng::u32>(kRow) * kH;
constexpr eng::u8 kPlanes = 4u;
const eng::ui::Rect kFull {0, 0, kW, kH};

constexpr eng::ui::UiTheme kThemeA {.text = 5u};
constexpr eng::ui::UiTheme kThemeB {.text = 3u};

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
eng::u32 count_color(eng::u8 color, eng::s16 x0, eng::s16 x1, eng::s16 y0, eng::s16 y1) {
	eng::u32 n = 0u;
	for (eng::s16 y = y0; y <= y1; ++y) {
		for (eng::s16 x = x0; x <= x1; ++x) {
			if (pixel_at(x, y) == color) {
				++n;
			}
		}
	}
	return n;
}

} // namespace

int main() {
	// --- layout_stack_v (orden de creacion) ---
	eng::ui::Panel g;
	g.bounds = eng::ui::Rect {0, 0, 40u, 40u};
	eng::ui::Label k1;
	k1.bounds = eng::ui::Rect {0, 0, 8u, 6u};
	eng::ui::Label k2;
	k2.bounds = eng::ui::Rect {0, 0, 8u, 6u};
	eng::ui::Label k3;
	k3.bounds = eng::ui::Rect {0, 0, 8u, 6u};
	g.add_child(&k1);
	g.add_child(&k2);
	g.add_child(&k3);
	eng::ui::layout_stack_v(g, 2u);
	check(k1.bounds.y == 0 && k2.bounds.y == 8 && k3.bounds.y == 16,
	      "layout_stack_v: gap en orden de creacion");
	check(k1.bounds.x == 0 && k2.bounds.x == 0 && k3.bounds.x == 0, "layout_stack_v: x = padre");

	// --- layout_stack_h ---
	k1.bounds.w = 8u;
	k2.bounds.w = 10u;
	k3.bounds.w = 6u;
	eng::ui::layout_stack_h(g, 3u);
	check(k1.bounds.x == 0 && k2.bounds.x == 11 && k3.bounds.x == 24, "layout_stack_h: gap");

	// --- anchor ---
	eng::ui::Label w;
	w.bounds = eng::ui::Rect {0, 0, 8u, 6u};
	g.add_child(&w);
	eng::ui::anchor(w, eng::ui::Anchor::BottomRight);
	check(w.bounds.x == 32 && w.bounds.y == 34, "anchor BottomRight del padre");
	eng::ui::anchor(w, eng::ui::Anchor::Center);
	check(w.bounds.x == 16 && w.bounds.y == 17, "anchor Center");

	// --- measure de un boton ---
	eng::ui::Button btn;
	btn.text = "AB";
	const eng::ui::Rect mb = eng::ui::measure(btn, kThemeA);
	check(mb.w == static_cast<eng::u16>(eng::ui::text_width("AB") + 2u * kThemeA.pad_x) &&
		      mb.h == kThemeA.btn_h,
	      "measure Button = texto + 2*pad_x x btn_h");

	// --- mark_all_dirty ---
	k1.clear_flag(eng::ui::WfDirty);
	k2.clear_flag(eng::ui::WfDirty);
	eng::ui::mark_all_dirty(g);
	check(k1.has(eng::ui::WfDirty) && k2.has(eng::ui::WfDirty), "mark_all_dirty marca el arbol");

	// --- cambio de tema -> recolorea ---
	alignas(2) eng::u8 planes[kPlaneStride * kPlanes] {};
	g_planes = planes;
	eng::field::ContiguousPlayfield pf {};
	check(pf.bind_raw(planes, sizeof(planes), kW, kH, kPlanes), "bind_raw del playfield");
	eng::field::Surface surf {pf, eng::field::SurfaceRect {0, 0, kW, kH}};

	eng::ui::Label lbl;
	lbl.bounds = eng::ui::Rect {2, 2, 0u, 0u};
	lbl.text = "A";

	eng::ui::UiPainter pa {surf, nullptr, kThemeA};
	pa.fill(kFull, 0u);
	eng::ui::draw_widget(lbl, pa);
	check(count_color(5u, 2, 9, 2, 9) > 0u, "tema A: texto en color 5");

	eng::ui::UiPainter pb {surf, nullptr, kThemeB};
	pb.fill(kFull, 0u);
	eng::ui::draw_widget(lbl, pb);
	check(count_color(3u, 2, 9, 2, 9) > 0u && count_color(5u, 2, 9, 2, 9) == 0u,
	      "tema B: recolorea a color 3");

	if (failures == 0) {
		std::printf("OK: GUI G5 (layout + tema) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
