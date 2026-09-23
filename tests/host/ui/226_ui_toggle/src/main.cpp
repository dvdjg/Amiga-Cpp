// ============================================================================
// Test HOST-226: GUI G3 — CheckBox y RadioButton (grupos).
// ============================================================================
//
// Valida `CheckBox` (alterna `*value` al soltar dentro) y `RadioButton` (activa uno y desactiva
// los hermanos del mismo `group_id`) sobre `UiContext`. Ver ROADMAP_GUI.md (G3).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/226_ui_toggle

#include <cstdio>

#include <eng/core/types/box.hpp>
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

eng::ui::UiEvent mouse(eng::ui::UiEventKind k, eng::s16 x, eng::s16 y) {
	eng::ui::UiEvent e;
	e.kind = k;
	e.x = x;
	e.y = y;
	return e;
}

void click(eng::ui::UiContext& ctx, eng::s16 x, eng::s16 y) {
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseDown, x, y));
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseUp, x, y));
}

constexpr eng::ui::UiTheme kT {};

} // namespace

int main() {
	eng::ui::Panel root;
	root.bounds = eng::ui::Rect {0, 0, 60u, 30u};

	// --- CheckBox ---
	bool cval = false;
	eng::ui::CheckBox c;
	c.bounds = eng::ui::Rect {0, 0, 20u, 10u};
	c.label = "X";
	c.value = &cval;
	root.add_child(&c);

	eng::ui::UiContext ctx;
	ctx.set_root(&root);

	click(ctx, 2, 2);
	check(cval, "CheckBox alterna a true");
	click(ctx, 2, 2);
	check(!cval, "CheckBox alterna a false");

	// soltar fuera no alterna
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseDown, 2, 2));
	ctx.dispatch(mouse(eng::ui::UiEventKind::MouseUp, 50, 20));
	check(!cval, "CheckBox: soltar fuera no alterna");

	// deshabilitado no alterna
	c.clear_flag(eng::ui::WfEnabled);
	click(ctx, 2, 2);
	check(!cval, "CheckBox deshabilitado no alterna");
	c.set_flag(eng::ui::WfEnabled);

	// --- RadioButton (grupos) ---
	bool r1v = false;
	bool r2v = false;
	bool r3v = false;
	bool r4v = false;
	eng::ui::RadioButton r1;
	r1.bounds = eng::ui::Rect {0, 12, 20u, 8u};
	r1.value = &r1v;
	r1.group_id = 1u;
	eng::ui::RadioButton r2;
	r2.bounds = eng::ui::Rect {0, 20, 20u, 8u};
	r2.value = &r2v;
	r2.group_id = 1u;
	eng::ui::RadioButton r3;
	r3.bounds = eng::ui::Rect {20, 12, 20u, 8u};
	r3.value = &r3v;
	r3.group_id = 1u;
	eng::ui::RadioButton r4;
	r4.bounds = eng::ui::Rect {20, 20, 20u, 8u};
	r4.value = &r4v;
	r4.group_id = 2u;
	root.add_child(&r1);
	root.add_child(&r2);
	root.add_child(&r3);
	root.add_child(&r4);

	click(ctx, 2, 14); // r1
	check(r1v && !r2v && !r3v && !r4v, "radio: activa r1 y apaga el grupo 1");

	click(ctx, 2, 22); // r2
	check(!r1v && r2v && !r3v && !r4v, "radio: activa r2 y apaga r1");

	click(ctx, 22, 22); // r4 (grupo 2)
	check(r2v && r4v, "radio: otro grupo no se apaga");

	click(ctx, 22, 14); // r3
	check(!r1v && !r2v && r3v && r4v, "radio: activa r3 sin tocar el grupo 2");

	// --- measure ---
	check(eng::ui::measure(c, kT).h == kT.check_s, "measure CheckBox alto = check_s");
	check(eng::ui::measure(c, kT).w ==
		      static_cast<eng::u16>(kT.check_s + kT.pad_x + eng::ui::text_width(c.label)),
	      "measure CheckBox ancho");
	check(eng::ui::measure(r1, kT).h == kT.radio_s, "measure Radio alto = radio_s");

	if (failures == 0) {
		std::printf("OK: GUI G3 (CheckBox + RadioButton/grupos) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
