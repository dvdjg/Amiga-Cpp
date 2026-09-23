// ============================================================================
// Test HOST-262: GUI — Slider.
// ============================================================================
//
// Valida `eng/ui/slider.hpp`: mapeo posicion->valor por click, ajuste con flechas y dibujo de la
// pista + pomo. Ver GUI_LIBRARY.md §11.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/262_ui_slider

#include <cstdio>

#include <eng/core/types/box.hpp>
#include <eng/field/surface.hpp>
#include <eng/ui/context.hpp>
#include <eng/ui/slider.hpp>
#include <eng/ui/widgets.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

eng::ui::UiEvent mouse(eng::ui::UiEventKind k, eng::s16 x, eng::s16 y) {
	eng::ui::UiEvent e;
	e.kind = k;
	e.x = x;
	e.y = y;
	return e;
}
eng::ui::UiEvent key(eng::u16 k) {
	eng::ui::UiEvent e;
	e.kind = eng::ui::UiEventKind::KeyDown;
	e.key = k;
	return e;
}

constexpr eng::u16 kW = 32u;
constexpr eng::u16 kH = 16u;
constexpr eng::u16 kRow = static_cast<eng::u16>(((kW / 8u) + 3u) & ~3u);
constexpr eng::u32 kPlaneStride = static_cast<eng::u32>(kRow) * kH;
constexpr eng::u8 kPlanes = 4u;
const eng::ui::Rect kFull {0, 0, kW, kH};
constexpr eng::ui::UiTheme kT {.shine = 1u, .shadow = 2u, .fill_active = 4u, .edit_bg = 6u};

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

} // namespace

int main() {
	eng::ui::Panel root;
	root.bounds = eng::ui::Rect {0, 0, 120u, 20u};
	eng::s16 val = 0;
	eng::ui::Slider s;
	s.bounds = eng::ui::Rect {0, 0, 101u, 10u}; // w-1 = 100 -> 1 px/unidad
	s.min = 0;
	s.max = 100;
	s.value = &val;
	root.add_child(&s);
	eng::ui::UiContext ctx;
	ctx.set_root(&root);

	// --- click mapea posicion -> valor ---
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseDown, 0, 5));
	check(val == 0, "click en el inicio -> min");
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseUp, 0, 5));
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseDown, 100, 5));
	check(val == 100, "click en el final -> max");
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseUp, 100, 5));
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseDown, 50, 5));
	check(val == 50, "click en el medio -> ~mitad");
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseUp, 50, 5));

	// un MouseDown nuevo fija el valor; el MouseUp dentro lo actualiza a su posicion.
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseDown, 25, 5));
	check(val == 25, "MouseDown fija 25");
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseUp, 100, 5));
	check(val == 100, "MouseUp dentro actualiza a su posicion");

	// --- flechas con foco ---
	val = 50;
	ctx.set_focus(&s);
	ctx.dispatch(key(eng::ui::kKeyRight));
	check(val == 51, "Right sube 1");
	ctx.dispatch(key(eng::ui::kKeyLeft));
	ctx.dispatch(key(eng::ui::kKeyLeft));
	check(val == 49, "Left baja 1");

	// --- dibujo: pista + pomo ---
	alignas(2) eng::u8 planes[kPlaneStride * kPlanes] {};
	g_planes = planes;
	eng::field::ContiguousPlayfield pf {};
	check(pf.bind_raw(planes, sizeof(planes), kW, kH, kPlanes), "bind_raw");
	eng::field::Surface surf {pf, eng::field::SurfaceRect {0, 0, kW, kH}};
	eng::ui::UiPainter p {surf, nullptr, kT};
	p.fill(kFull, 0u);
	val = 0;
	eng::ui::Slider sd;
	sd.bounds = eng::ui::Rect {0, 0, 30u, 8u};
	sd.min = 0;
	sd.max = 100;
	sd.value = &val;
	eng::ui::draw_slider(sd, p);
	check(pixel_at(1, 3) == 4u, "pomo al inicio (fill_active)");
	p.fill(kFull, 0u);
	val = 100;
	eng::ui::draw_slider(sd, p);
	check(pixel_at(30, 3) == 4u, "pomo al final (w-2)");

	if (failures == 0) {
		std::printf("OK: GUI Slider (click/flechas/dibujo) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
