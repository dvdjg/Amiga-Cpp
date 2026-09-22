// ============================================================================
// Test HOST-229: GUI G6 — ventanas (Window/Popup/Toast/Dialog) y modalidad.
// ============================================================================
//
// Valida `eng/ui/window.hpp` y la politica de `UiContext`: Z/raise, modal que filtra hit-test y
// foco, Esc cierra el dialogo/popup, popup que se cierra al pulsar fuera y toast con TTL que no
// capta input. Ver ROADMAP_GUI.md (G6) y GUI_LIBRARY.md §13.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/229_ui_windows

#include <cstdio>

#include <eng/core/box.hpp>
#include <eng/ui/context.hpp>
#include <eng/ui/widgets.hpp>
#include <eng/ui/window.hpp>

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
eng::ui::UiEvent evk(eng::ui::UiEventKind k) {
	eng::ui::UiEvent e;
	e.kind = k;
	return e;
}

} // namespace

int main() {
	// --- Z / raise ---
	{
		eng::ui::Panel root;
		root.bounds = eng::ui::Rect {0, 0, 60u, 40u};
		eng::ui::Window w1;
		w1.bounds = eng::ui::Rect {0, 0, 20u, 20u};
		eng::ui::Window w2;
		w2.bounds = eng::ui::Rect {10, 0, 20u, 20u};
		root.add_child(&w1);
		root.add_child(&w2); // w2 al frente
		eng::ui::UiContext ctx;
		ctx.set_root(&root);
		check(ctx.hit_test(&root, 15, 5) == &w2, "hit-test: el de delante (w2)");
		eng::ui::raise(w1);
		check(ctx.hit_test(&root, 15, 5) == &w1, "raise(w1): pasa al frente");
		check(root.first_child == &w1 && w1.next == &w2, "raise reenlaza la lista");
	}

	// --- modalidad ---
	eng::ui::Panel root;
	root.bounds = eng::ui::Rect {0, 0, 60u, 40u};
	eng::ui::Button desktop;
	desktop.bounds = eng::ui::Rect {0, 0, 10u, 8u};
	eng::ui::Window dlg;
	dlg.bounds = eng::ui::Rect {20, 20, 20u, 18u};
	eng::ui::window_set_kind(dlg, eng::ui::WindowKind::Dialog);
	eng::ui::Button inside;
	inside.bounds = eng::ui::Rect {22, 22, 14u, 10u};
	root.add_child(&desktop);
	dlg.add_child(&inside);
	root.add_child(&dlg);

	eng::ui::UiContext ctx;
	ctx.set_root(&root);

	check(ctx.top_modal() == &dlg, "top_modal = dialogo");
	check(ctx.hit_test(&dlg, 5, 4) == nullptr, "fuera del modal no hay hit");
	check(ctx.hit_test(&dlg, 25, 25) == &inside, "dentro del modal si hay hit");
	check(!ctx.dispatch(mouse(eng::ui::UiEventKind::MouseDown, 5, 4)),
	      "el modal bloquea el escritorio");

	// --- Esc cierra el dialogo ---
	check(!ctx.dispatch(evk(eng::ui::UiEventKind::KeyDown)), "KeyDown sin foco no consume");
	{
		eng::ui::UiEvent esc = evk(eng::ui::UiEventKind::KeyDown);
		esc.key = eng::ui::kKeyEsc;
		check(ctx.dispatch(esc), "Esc consume");
		check(!dlg.has(eng::ui::WfVisible), "Esc cierra el dialogo");
	}

	// --- Toast: TTL y sin input ---
	eng::ui::Window toast;
	toast.bounds = eng::ui::Rect {0, 0, 10u, 6u};
	eng::ui::window_set_kind(toast, eng::ui::WindowKind::Toast);
	toast.ttl = 2u;
	root.add_child(&toast);
	check(ctx.hit_test(&root, 5, 3) != &toast, "el toast no capta input");
	ctx.dispatch(evk(eng::ui::UiEventKind::Tick));
	check(toast.ttl == 1u, "Tick decrementa el TTL");
	ctx.dispatch(evk(eng::ui::UiEventKind::Tick));
	check(!toast.has(eng::ui::WfVisible), "el toast expira a 0");

	// --- Popup: se cierra al pulsar fuera ---
	eng::ui::Window popup;
	popup.bounds = eng::ui::Rect {30, 30, 20u, 8u};
	eng::ui::window_set_kind(popup, eng::ui::WindowKind::Popup);
	root.add_child(&popup);
	check(ctx.dispatch(mouse(eng::ui::UiEventKind::MouseDown, 0, 0)),
	      "click fuera del popup se consume");
	check(!popup.has(eng::ui::WfVisible), "el popup se cierra al pulsar fuera");

	if (failures == 0) {
		std::printf("OK: GUI G6 (ventanas + modalidad) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
