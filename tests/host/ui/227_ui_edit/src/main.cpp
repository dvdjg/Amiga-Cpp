// ============================================================================
// Test HOST-227: GUI G4 — foco de teclado (Tab) y EditBox.
// ============================================================================
//
// Valida `eng/ui/editbox.hpp` (buffer externo, insercion/borrado, caret y vista horizontal) y el
// foco de `UiContext` (`Tab`/`Shift+Tab` cicla entre widgets que aceptan foco). Ver
// ROADMAP_GUI.md (G4) y GUI_LIBRARY.md §10-§11.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/227_ui_edit

#include <cstdio>
#include <cstring>

#include <eng/core/box.hpp>
#include <eng/ui/context.hpp>
#include <eng/ui/editbox.hpp>
#include <eng/ui/widgets.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

eng::ui::UiEvent key(eng::u16 k, bool shift = false) {
	eng::ui::UiEvent e;
	e.kind = eng::ui::UiEventKind::KeyDown;
	e.key = k;
	e.shift = shift;
	return e;
}

} // namespace

int main() {
	eng::ui::Panel root;
	root.bounds = eng::ui::Rect {0, 0, 80u, 40u};

	char b1[16] = "abc";
	char b2[16] = "";
	eng::ui::EditBox e1;
	e1.bounds = eng::ui::Rect {0, 0, 40u, 12u};
	e1.buf = b1;
	e1.cap = sizeof(b1);
	e1.len = 3u;
	e1.caret = 3u;
	eng::ui::EditBox e2;
	e2.bounds = eng::ui::Rect {0, 14, 40u, 12u};
	e2.buf = b2;
	e2.cap = sizeof(b2);
	root.add_child(&e1);
	root.add_child(&e2);

	eng::ui::UiContext ctx;
	ctx.set_root(&root);

	// --- foco con Tab / Shift+Tab ---
	// El orden de foco es el del arbol (Z: el ultimo anadido va al frente -> e2, e1).
	ctx.focus_next(false);
	check(ctx.focus == &e2 && e2.has(eng::ui::WfFocused), "Tab: primer foco = e2 (frente)");
	ctx.focus_next(false);
	check(ctx.focus == &e1 && !e2.has(eng::ui::WfFocused) && e1.has(eng::ui::WfFocused),
	      "Tab: pasa a e1 y limpia e2");
	ctx.focus_next(false);
	check(ctx.focus == &e2, "Tab: vuelve a e2 (wrap)");
	ctx.focus_next(true);
	check(ctx.focus == &e1, "Shift+Tab: retrocede a e1");

	// --- Tab no inserta texto ---
	ctx.set_focus(&e1);
	const eng::u16 len_before = e1.len;
	ctx.dispatch(key(eng::ui::kKeyTab));
	check(e1.len == len_before && ctx.focus == &e2, "Tab no inserta y cambia el foco");
	ctx.dispatch(key(eng::ui::kKeyTab, true));
	check(ctx.focus == &e1, "Shift+Tab vuelve a e1");

	// --- insercion en medio ---
	ctx.set_focus(&e1);
	e1.caret = 1u;
	ctx.dispatch(key(static_cast<eng::u16>('X')));
	check(std::strcmp(b1, "aXbc") == 0 && e1.len == 4u && e1.caret == 2u,
	      "insert en medio -> aXbc");

	// --- backspace ---
	ctx.dispatch(key(eng::ui::kKeyBackspace));
	check(std::strcmp(b1, "abc") == 0 && e1.caret == 1u, "backspace borra el de la izquierda");

	// --- delete ---
	ctx.dispatch(key(eng::ui::kKeyDelete));
	check(std::strcmp(b1, "ac") == 0 && e1.len == 2u, "delete borra el de la derecha");

	// --- left / right / home / end ---
	ctx.dispatch(key(eng::ui::kKeyEnd));
	check(e1.caret == e1.len, "End -> caren al final");
	ctx.dispatch(key(eng::ui::kKeyHome));
	check(e1.caret == 0u, "Home -> caret al principio");
	ctx.dispatch(key(eng::ui::kKeyRight));
	check(e1.caret == 1u, "Right avanza el caret");
	ctx.dispatch(key(eng::ui::kKeyLeft));
	check(e1.caret == 0u, "Left retrocede el caret");

	// --- limite de capacidad ---
	char tiny[3] = ""; // 2 chars utiles + NUL
	eng::ui::EditBox et;
	et.bounds = eng::ui::Rect {0, 28, 40u, 10u};
	et.buf = tiny;
	et.cap = sizeof(tiny);
	root.add_child(&et);
	ctx.set_focus(&et);
	ctx.dispatch(key(static_cast<eng::u16>('a')));
	ctx.dispatch(key(static_cast<eng::u16>('b')));
	ctx.dispatch(key(static_cast<eng::u16>('c'))); // no cabe
	check(et.len == 2u && std::strcmp(tiny, "ab") == 0, "capacidad limita a cap-1");

	// --- vista horizontal (ensure_caret_visible) ---
	char wide[16] = "abcdefgh";
	eng::ui::EditBox ew;
	ew.bounds = eng::ui::Rect {0, 0, 24u, 10u}; // cols = (24-8)/8 = 2
	ew.buf = wide;
	ew.cap = sizeof(wide);
	ew.len = 8u;
	ew.caret = 2u;
	ew.view = 0u;
	ew.ensure_caret_visible();
	check(ew.view == 1u, "caret en el borde -> desplaza la vista");
	ew.caret = 0u;
	ew.ensure_caret_visible();
	check(ew.view == 0u, "caret a la izquierda -> vista a 0");

	if (failures == 0) {
		std::printf("OK: GUI G4 (foco + EditBox) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
