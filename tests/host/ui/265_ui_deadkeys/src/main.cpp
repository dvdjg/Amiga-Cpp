// Test HOST-265: teclas muertas (composición de acentos) en `eng::ui::keymap`.
//
// Comprueba que `compose` produce los Latin-1 precompuestos, que `dead_key_of` reconoce
// las teclas de acento con Alt, que `rawkey_to_char` encadena muerta + letra, y que
// `dispatch_msg` inserta el carácter compuesto en un EditBox (entrada por mensajes).
//
//   CXX=<g++> bash tools/run-host-tests.sh tests/host/ui/265_ui_deadkeys

#include <cstdio>

#include <eng/os/message.hpp>
#include <eng/ui/msg_adapter.hpp>

using eng::ui::DeadKey;
using eng::ui::KeyboardLayout;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

eng::os::Msg key_msg(eng::u16 raw, eng::u16 qual) {
	eng::os::Msg m {};
	m.type = eng::os::MsgType::KeyDown;
	m.payload.key = {raw, qual};
	return m;
}

} // namespace

int main() {
	std::printf("== HOST-265 teclas muertas ==\n");

	// --- compose: Latin-1 precompuesto --------------------------------------
	check(eng::ui::compose(DeadKey::Acute, 'e') == 0xe9u, "agudo+e = é");
	check(eng::ui::compose(DeadKey::Acute, 'E') == 0xc9u, "agudo+E = É");
	check(eng::ui::compose(DeadKey::Grave, 'a') == 0xe0u, "grave+a = à");
	check(eng::ui::compose(DeadKey::Circumflex, 'o') == 0xf4u, "circ+o = ô");
	check(eng::ui::compose(DeadKey::Tilde, 'n') == 0xf1u, "tilde+n = ñ");
	check(eng::ui::compose(DeadKey::Tilde, 'N') == 0xd1u, "tilde+N = Ñ");
	check(eng::ui::compose(DeadKey::Diaeresis, 'u') == 0xfcu, "dieresis+u = ü");
	check(eng::ui::compose(DeadKey::Diaeresis, 'y') == 0xffu, "dieresis+y = ÿ");
	check(eng::ui::compose(DeadKey::Cedilla, 'c') == 0xe7u, "cedilla+c = ç");
	check(eng::ui::compose(DeadKey::Ring, 'a') == 0xe5u, "anillo+a = å");
	check(eng::ui::compose(DeadKey::Acute, 'x') == 0u, "agudo+x no combina");
	check(eng::ui::compose(DeadKey::Tilde, 'e') == 0u, "tilde+e no combina");
	check(eng::ui::compose(DeadKey::None, 'a') == 0u, "sin muerta = 0");

	// --- dead_key_of: teclas de acento con Alt ------------------------------
	check(eng::ui::dead_key_of(0x25u) == DeadKey::Acute, "Alt+H = agudo");
	check(eng::ui::dead_key_of(0x27u) == DeadKey::Tilde, "Alt+K = tilde");
	check(eng::ui::dead_key_of(0x33u) == DeadKey::Cedilla, "Alt+C = cedilla");
	check(eng::ui::dead_key_of(0x31u) == DeadKey::None, "Z no es tecla muerta");

	// --- rawkey_to_char: encadena muerta + letra ----------------------------
	{
		eng::ui::DeadKeyState st {};
		// Alt+H (0x25) -> acento agudo pendiente, sin carácter.
		const eng::ui::KeyChar k1 = eng::ui::rawkey_to_char(st, 0x25u, false, true);
		check(k1.consumed && k1.ch == 0u, "Alt+H queda pendiente");
		check(st.pending == DeadKey::Acute, "pendiente = agudo");
		// 'e' (0x12) -> é compuesta.
		const eng::ui::KeyChar k2 = eng::ui::rawkey_to_char(st, 0x12u, false, false);
		check(!k2.consumed && k2.ch == 0xe9u, "e tras agudo = é");
		check(st.pending == DeadKey::None, "pendiente consumida");
	}
	{
		eng::ui::DeadKeyState st {};
		(void)eng::ui::rawkey_to_char(st, 0x27u, false, true); // Alt+K tilde
		const eng::ui::KeyChar k = eng::ui::rawkey_to_char(st, 0x36u, false, false); // n
		check(k.ch == 0xf1u, "n tras tilde = ñ");
	}
	{
		eng::ui::DeadKeyState st {};
		(void)eng::ui::rawkey_to_char(st, 0x25u, false, true); // agudo
		const eng::ui::KeyChar k = eng::ui::rawkey_to_char(st, 0x32u, false, false); // x
		check(k.ch == 'x', "muerta que no combina cae a la letra");
		check(st.pending == DeadKey::None, "pendiente descartada");
	}

	// --- dispatch_msg: Alt+H + e inserta 'é' en un EditBox -------------------
	{
		eng::ui::Panel root;
		root.bounds = eng::ui::Rect {0, 0, 40u, 12u};
		char buf[8] = "";
		eng::ui::EditBox e;
		e.bounds = eng::ui::Rect {0, 0, 30u, 10u};
		e.buf = buf;
		e.cap = sizeof(buf);
		root.add_child(&e);
		eng::ui::UiContext ctx;
		ctx.set_root(&root);
		ctx.set_focus(&e);

		check(eng::ui::dispatch_msg(ctx, key_msg(0x25u, eng::os::kQualAlt)), "Alt+H consumida");
		check(eng::ui::dispatch_msg(ctx, key_msg(0x12u, 0u)), "e consumida");
		// El EditBox guarda UTF-8: 'é' (U+00E9) son dos bytes 0xC3 0xA9.
		check(static_cast<eng::u8>(buf[0]) == 0xc3u && static_cast<eng::u8>(buf[1]) == 0xa9u &&
			      e.len == 2u,
		      "EditBox tiene 'é' (UTF-8)");
	}

	if (g_fail == 0) {
		std::printf("OK: teclas muertas (composicion de acentos) validadas.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es)\n", g_fail);
	return 1;
}
