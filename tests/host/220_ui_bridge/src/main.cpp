// ============================================================================
// Test HOST-220: puente Msg -> UiEvent (eng/ui/ui_bridge.hpp).
// ============================================================================
//
// Valida que cada `MsgType` de entrada se traduce a su `UiEvent` (posicion, boton, tecla,
// modificadores, joystick/pad) y que los tipos que no son de entrada se descartan.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/220_ui_bridge

#include <cstdio>

#include <eng/ui/ui_bridge.hpp>

using namespace eng;
using namespace eng::os;
using namespace eng::ui;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

void test_mouse() {
	Msg m {};
	m.type = MsgType::MouseMove;
	m.payload.mouse = {100, 50, 0, 0, 3};
	UiEvent e {};
	check(to_ui_event(m, e) && e.kind == UiEventKind::MouseMove, "MouseMove");
	check(e.x == 100 && e.y == 50 && e.buttons == 3, "MouseMove: posicion y botones");

	m.type = MsgType::MouseButton;
	m.payload.mouse = {100, 50, 0, 0, 1};
	check(to_ui_event(m, e) && e.kind == UiEventKind::MouseDown, "MouseDown (bit 0)");
	m.payload.mouse.buttons = 0;
	check(to_ui_event(m, e) && e.kind == UiEventKind::MouseUp, "MouseUp");
}

void test_key() {
	Msg m {};
	m.type = MsgType::KeyDown;
	m.payload.key = {0x45, kQualShift | kQualCtrl};
	UiEvent e {};
	check(to_ui_event(m, e) && e.kind == UiEventKind::KeyDown, "KeyDown");
	check(e.key == 0x45 && e.shift && e.ctrl && !e.alt, "KeyDown: tecla y modificadores");

	m.type = MsgType::KeyUp;
	m.payload.key = {0x45, 0};
	check(to_ui_event(m, e) && e.kind == UiEventKind::KeyUp && e.key == 0x45, "KeyUp");
}

void test_joy() {
	Msg m {};
	m.type = MsgType::Joystick;
	m.payload.joy = {2, 0x05, 1};
	UiEvent e {};
	check(to_ui_event(m, e) && e.kind == UiEventKind::JoyButton, "Joystick");
	check(e.joy_port == 2, "Joystick: puerto");
	check((e.joy_buttons & 0x05u) != 0u && (e.joy_buttons & 0x0100u) != 0u,
	      "Joystick: dirs + fuego");

	m.type = MsgType::Gamepad;
	m.payload.pad = {2, 0x0042};
	check(to_ui_event(m, e) && e.kind == UiEventKind::JoyButton && e.joy_buttons == 0x0042u,
	      "Gamepad: bitmask");
}

void test_discard() {
	UiEvent e {};
	Msg m {};
	m.type = MsgType::VBlank;
	check(!to_ui_event(m, e), "VBlank no es de entrada");
	m.type = MsgType::Timer;
	check(!to_ui_event(m, e), "Timer no es de entrada");
	m.type = MsgType::FileDone;
	check(!to_ui_event(m, e), "FileDone no es de entrada");
	m.type = MsgType::User;
	check(!to_ui_event(m, e), "User no es de entrada");
	m.type = MsgType::Quit;
	check(!to_ui_event(m, e), "Quit no es de entrada");
}

} // namespace

int main() {
	test_mouse();
	test_key();
	test_joy();
	test_discard();

	if (failures == 0) {
		std::printf("OK: puente Msg->UiEvent (raton, teclado, joystick, descartes) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
