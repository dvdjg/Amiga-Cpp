// ============================================================================
// Test HOST-261: GUI — entrada por mensajes (keymap rawkey + dispatch_msg).
// ============================================================================
//
// Valida `eng/ui/keymap.hpp` (rawkey Amiga -> tecla logica) y `eng/ui/msg_adapter.hpp`
// (`dispatch_msg`: `os::Msg` de entrada -> `UiEvent` -> `UiContext`). La GUI consume la entrada
// por MENSAJES, no por sondeo. Ver MINI_OS_INPUT.md y GUI_LIBRARY.md §15.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/261_ui_msg_input

#include <cstdio>
#include <cstring>

#include <eng/os/message.hpp>
#include <eng/ui/editbox.hpp>
#include <eng/ui/msg_adapter.hpp>
#include <eng/ui/widgets.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

eng::os::Msg key_msg(eng::u8 rawkey, eng::u16 qual) {
	eng::os::Msg m {};
	m.type = eng::os::MsgType::KeyDown;
	m.payload.key = {rawkey, qual};
	return m;
}

} // namespace

int main() {
	// --- keymap rawkey -> logica ---
	check(eng::ui::rawkey_to_key(0x20u, false) == static_cast<eng::u16>('a'), "0x20 -> 'a'");
	check(eng::ui::rawkey_to_key(0x20u, true) == static_cast<eng::u16>('A'), "0x20+shift -> 'A'");
	check(eng::ui::rawkey_to_key(0x01u, false) == static_cast<eng::u16>('1'), "0x01 -> '1'");
	check(eng::ui::rawkey_to_key(0x01u, true) == static_cast<eng::u16>('!'), "0x01+shift -> '!'");
	check(eng::ui::rawkey_to_key(0x2au, false) == static_cast<eng::u16>('\''), "0x2A -> apostrofo");
	check(eng::ui::rawkey_to_key(0x41u, false) == eng::ui::kKeyBackspace, "0x41 -> Backspace");
	check(eng::ui::rawkey_to_key(0x42u, false) == eng::ui::kKeyTab, "0x42 -> Tab");
	check(eng::ui::rawkey_to_key(0x44u, false) == eng::ui::kKeyReturn, "0x44 -> Return");
	check(eng::ui::rawkey_to_key(0x45u, false) == eng::ui::kKeyEsc, "0x45 -> Esc");
	check(eng::ui::rawkey_to_key(0x4fu, false) == eng::ui::kKeyLeft, "0x4F -> Left");
	check(eng::ui::rawkey_to_key(0x4eu, false) == eng::ui::kKeyRight, "0x4E -> Right");

	// --- dispatch_msg: teclado por mensajes a un EditBox con foco ---
	eng::ui::Panel root;
	root.bounds = eng::ui::Rect {0, 0, 60u, 20u};
	char buf[16] = "";
	eng::ui::EditBox e;
	e.bounds = eng::ui::Rect {0, 0, 40u, 12u};
	e.buf = buf;
	e.cap = sizeof(buf);
	root.add_child(&e);
	eng::ui::UiContext ctx;
	ctx.set_root(&root);
	ctx.set_focus(&e);

	check(eng::ui::dispatch_msg(ctx, key_msg(0x20u, 0u)), "KeyDown 'a' consumido");
	check(eng::ui::dispatch_msg(ctx, key_msg(0x21u, 0u)), "KeyDown 's' consumido"); // s
	check(eng::ui::dispatch_msg(ctx, key_msg(0x20u, eng::os::kQualShift)), "KeyDown 'A'");
	check(std::strcmp(buf, "asA") == 0 && e.len == 3u, "EditBox recibio asA");
	check(eng::ui::dispatch_msg(ctx, key_msg(0x41u, 0u)), "Backspace consumido");
	check(std::strcmp(buf, "as") == 0 && e.len == 2u, "Backspace borro la A");

	// un Msg que no es de entrada no se consume.
	eng::os::Msg vb {};
	vb.type = eng::os::MsgType::VBlank;
	check(!eng::ui::dispatch_msg(ctx, vb), "VBlank no se consume");

	// --- dispatch_msg: raton (MouseButton) a un Button ---
	int clicks = 0;
	eng::ui::Button btn;
	btn.bounds = eng::ui::Rect {0, 0, 20u, 12u};
	btn.text = "ok";
	btn.on_click = [](void* u) { ++*static_cast<int*>(u); };
	btn.user = &clicks;
	eng::ui::Panel root2;
	root2.bounds = eng::ui::Rect {0, 0, 40u, 20u};
	root2.add_child(&btn);
	eng::ui::UiContext ctx2;
	ctx2.set_root(&root2);
	eng::os::Msg down {};
	down.type = eng::os::MsgType::MouseButton;
	down.payload.mouse = {5, 5, 0, 0, 1u}; // x, y, dx, dy, buttons
	eng::os::Msg up {};
	up.type = eng::os::MsgType::MouseButton;
	up.payload.mouse = {5, 5, 0, 0, 0u};
	eng::ui::dispatch_msg(ctx2, down);
	eng::ui::dispatch_msg(ctx2, up);
	check(clicks == 1, "click por mensajes dispara on_click");

	if (failures == 0) {
		std::printf("OK: GUI entrada por mensajes (keymap + dispatch_msg) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
