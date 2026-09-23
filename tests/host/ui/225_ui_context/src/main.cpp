// ============================================================================
// Test HOST-225: GUI G2 — UiContext (hit-test, foco, despacho) y Button.
// ============================================================================
//
// Valida `eng/ui/context.hpp` (`UiContext`) y `Button` (`eng/ui/widgets.hpp`): hit-test de
// delante hacia atras, `on_click` al soltar dentro (una vez), soltar fuera no dispara, un
// deshabilitado no recibe el hit, foco al pulsar y dibujo del boton (bisel segun `WfPressed`).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/225_ui_context

#include <cstdio>

#include <eng/core/types/box.hpp>
#include <eng/field/surface.hpp>
#include <eng/ui/context.hpp>
#include <eng/ui/widgets.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

int clicks_a = 0;
int clicks_b = 0;
void on_a(void*) { ++clicks_a; }
void on_b(void*) { ++clicks_b; }

eng::ui::UiEvent mouse(eng::ui::UiEventKind k, eng::s16 x, eng::s16 y) {
	eng::ui::UiEvent e;
	e.kind = k;
	e.x = x;
	e.y = y;
	return e;
}

constexpr eng::u16 kW = 32u;
constexpr eng::u16 kH = 16u;
constexpr eng::u16 kRow = static_cast<eng::u16>(((kW / 8u) + 3u) & ~3u);
constexpr eng::u32 kPlaneStride = static_cast<eng::u32>(kRow) * kH;
constexpr eng::u8 kPlanes = 4u;
const eng::ui::Rect kFull {0, 0, kW, kH};

constexpr eng::ui::UiTheme kT {
	.shine = 1u, .shadow = 2u, .fill = 3u, .fill_active = 4u, .text = 5u,
	.panel_frame = eng::ui::FrameStyle::Raised, .button_frame = eng::ui::FrameStyle::Raised,
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

} // namespace

int main() {
	eng::ui::Panel root;
	root.bounds = eng::ui::Rect {0, 0, 32u, 16u};
	eng::ui::Button a;
	a.bounds = eng::ui::Rect {0, 0, 20u, 12u};
	a.text = "A";
	a.on_click = on_a;
	eng::ui::Button b;
	b.bounds = eng::ui::Rect {10, 0, 20u, 12u}; // solapa con A en x 10..19
	b.text = "B";
	b.on_click = on_b;
	root.add_child(&a);
	root.add_child(&b); // al frente

	eng::ui::UiContext ctx;
	ctx.set_root(&root);

	// --- hit-test de delante hacia atras ---
	check(ctx.hit_test(&root, 15, 5) == &b, "hit-test devuelve el de delante");
	check(ctx.hit_test(&root, 5, 5) == &a, "hit-test en zona exclusiva de A");
	check(ctx.hit_test(&root, 100, 100) == nullptr, "hit-test fuera = nullptr");

	// --- click dentro dispara on_click una vez ---
	clicks_a = 0;
	clicks_b = 0;
	check(ctx.dispatch(mouse(eng::ui::UiEventKind::MouseDown, 5, 5)), "MouseDown consume");
	check(a.has(eng::ui::WfPressed), "MouseDown marca pressed");
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseUp, 5, 5));
	check(clicks_a == 1 && clicks_b == 0, "soltar dentro dispara on_click una vez");
	check(!a.has(eng::ui::WfPressed), "MouseUp limpia pressed");

	// --- soltar fuera no dispara ---
	clicks_a = 0;
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseDown, 5, 5));
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseUp, 30, 5)); // fuera de A
	check(clicks_a == 0, "soltar fuera no dispara");

	// --- MouseDown fuera de todo no consume ---
	check(!ctx.dispatch(mouse(eng::ui::UiEventKind::MouseDown, 100, 100)),
	      "MouseDown fuera no consume");

	// --- en el solape gana el de delante ---
	clicks_a = 0;
	clicks_b = 0;
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseDown, 15, 5));
	check(b.has(eng::ui::WfPressed) && !a.has(eng::ui::WfPressed),
	      "MouseDown en solape -> B (delante)");
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseUp, 15, 5));
	check(clicks_b == 1 && clicks_a == 0, "solo B dispara");
	check(ctx.focus == &b, "foco al widget pulsado");

	// --- un deshabilitado se salta ---
	b.clear_flag(eng::ui::WfEnabled);
	check(ctx.hit_test(&root, 15, 5) == &a, "hit-test salta el deshabilitado");

	// --- dibujo del boton (bisel segun WfPressed) ---
	alignas(2) eng::u8 planes[kPlaneStride * kPlanes] {};
	g_planes = planes;
	eng::field::ContiguousPlayfield pf {};
	check(pf.bind_raw(planes, sizeof(planes), kW, kH, kPlanes), "bind_raw del playfield");
	eng::field::Surface surf {pf, eng::field::SurfaceRect {0, 0, kW, kH}};
	eng::ui::UiPainter p {surf, nullptr, kT};

	a.clear_flag(eng::ui::WfPressed);
	p.fill(kFull, 0u);
	eng::ui::draw_widget(a, p);
	check(pixel_at(10, 0) == 1u, "boton normal: shine arriba");
	a.set_flag(eng::ui::WfPressed);
	p.fill(kFull, 0u);
	eng::ui::draw_widget(a, p);
	check(pixel_at(10, 0) == 2u, "boton pulsado: shadow arriba (hundido)");

	if (failures == 0) {
		std::printf("OK: GUI G2 (UiContext + hit-test + Button) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
