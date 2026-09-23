// ============================================================================
// Test HOST-303: EditBox en UTF-8 (edicion por code point).
// ============================================================================
//
// Valida `eng::ui::EditBox` con texto multibyte: inserta code points (ASCII, Latin-1 y cirilico),
// codifica en UTF-8, y borra/mueve el caret por **code point** (no por byte). Comprueba tambien
// que el cirilico llega al campo por la via de eventos (`event_edit`).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/303_ui_editbox_utf8

#include <cstdio>

#include <eng/ui/context.hpp>
#include <eng/ui/editbox.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", m);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-303 editbox utf8 ==\n");

	char buf[32] = "";
	eng::ui::EditBox e;
	e.buf = buf;
	e.cap = sizeof(buf);

	// --- inserta cirilico (U+041F П -> 0xD0 0x9F) ---
	check(e.insert_cp(0x041fu), "insert П");
	check(static_cast<eng::u8>(buf[0]) == 0xd0u && static_cast<eng::u8>(buf[1]) == 0x9fu,
	      "П codificado como 0xD0 0x9F");
	check(e.len == 2u && e.caret == 2u, "len/caret en bytes (2)");

	// --- inserta ASCII ---
	check(e.insert('e'), "insert 'e'");
	check(e.len == 3u && buf[2] == 'e', "ASCII 1 byte");

	// --- backspace borra el code point anterior completo ---
	check(e.backspace(), "backspace 1");
	check(e.len == 2u && static_cast<eng::u8>(buf[1]) == 0x9fu, "borra 'e' (len 2)");
	check(e.backspace(), "backspace 2");
	check(e.len == 0u && buf[0] == '\0', "borra П completo (len 0)");

	// --- delete borra el code point bajo el caret ---
	e.insert_cp(0x0440u); // р -> 0xD1 0x80
	e.insert_cp(0x0438u); // и -> 0xD0 0xB8
	e.caret = 0u;
	check(e.del(), "del 1");
	check(e.len == 2u && static_cast<eng::u8>(buf[0]) == 0xd0u && static_cast<eng::u8>(buf[1]) == 0xb8u,
	      "del borra 'р' y deja 'и'");
	check(e.del(), "del 2");
	check(e.len == 0u, "campo vacio");

	// --- mover por code point ---
	e.insert_cp(0x041fu); // П
	e.insert_cp(0x0440u); // р
	check(e.caret == 4u, "caret al final (4 bytes)");
	e.move_left();
	check(e.caret == 2u, "left -> 2 bytes (code point anterior)");
	e.move_left();
	check(e.caret == 0u, "left -> 0");
	e.move_right();
	check(e.caret == 2u, "right -> 2");

	// --- Latin-1 (é = U+00E9 -> 0xC3 0xA9) ---
	char buf2[8] = "";
	eng::ui::EditBox e2;
	e2.buf = buf2;
	e2.cap = sizeof(buf2);
	check(e2.insert_cp(0x00e9u), "insert é");
	check(static_cast<eng::u8>(buf2[0]) == 0xc3u && static_cast<eng::u8>(buf2[1]) == 0xa9u,
	      "é codificado como 0xC3 0xA9");

	// --- via de eventos: un KeyDown cirilico entra en el campo ---
	{
		eng::ui::Panel root;
		root.bounds = eng::ui::Rect {0, 0, 40u, 12u};
		char buf3[16] = "";
		eng::ui::EditBox e3;
		e3.bounds = eng::ui::Rect {0, 0, 30u, 10u};
		e3.buf = buf3;
		e3.cap = sizeof(buf3);
		root.add_child(&e3);
		eng::ui::UiContext ctx;
		ctx.set_root(&root);
		ctx.set_focus(&e3);

		eng::ui::UiEvent k {};
		k.kind = eng::ui::UiEventKind::KeyDown;
		k.key = 0x041fu; // П
		check(ctx.dispatch(k), "KeyDown cirilico consumido");
		check(e3.len == 2u && static_cast<eng::u8>(buf3[0]) == 0xd0u, "П insertado por evento");
	}

	if (g_fail == 0) {
		std::printf("OK: EditBox UTF-8 (edicion por code point) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es)\n", g_fail);
	return 1;
}
