// ============================================================================
// Test HOST-252: productores de entrada del mini-SO (eng/os/input.hpp).
// ============================================================================
//
// Valida que joystick/gamepad/raton emiten un mensaje SOLO cuando cambia el estado (un registro
// que no cambia no genera mensaje), y que el raton mantiene la posicion absoluta clampada.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/os/252_os_input

#include <cstdio>

#include <eng/os/input.hpp>

using namespace eng;
using namespace eng::os;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

void test_joy() {
	JoyProducer j;
	Msg m {};
	check(j.update(0x01u, 0u, 5u, m), "joy: la primera muestra emite");
	check(m.type == MsgType::Joystick && m.payload.joy.dirs == 0x01u && m.payload.joy.port == 2u,
	      "joy: payload");
	check(!j.update(0x01u, 0u, 6u, m), "joy: sin cambio no emite");
	check(j.update(0x01u, 1u, 7u, m) && m.payload.joy.fire == 1u, "joy: cambio de fuego emite");
	check(j.update(0x08u, 1u, 8u, m) && m.payload.joy.dirs == 0x08u, "joy: cambio de direccion emite");
}

void test_pad() {
	PadProducer p;
	Msg m {};
	check(p.update(0x0042u, 1u, m) && m.type == MsgType::Gamepad &&
	      m.payload.pad.buttons == 0x0042u, "pad: emite con bitmask");
	check(!p.update(0x0042u, 2u, m), "pad: sin cambio no emite");
	check(p.update(0x0043u, 3u, m), "pad: cambio emite");
}

void test_mouse() {
	MouseProducer mo;
	Msg m {};
	check(!mo.update(100u, 100u, 0u, 1u, m), "raton: la primera muestra no emite movimiento");
	check(mo.update(110u, 100u, 0u, 2u, m), "raton: el movimiento emite");
	check(m.type == MsgType::MouseMove && m.payload.mouse.dx == 10 && mo.x == 170,
	      "raton: delta y posicion absoluta");
	check(!mo.update(110u, 100u, 0u, 3u, m), "raton: sin cambio no emite");
	check(mo.update(110u, 100u, 1u, 4u, m) && m.type == MsgType::MouseButton,
	      "raton: el boton emite");
	check(!mo.update(110u, 100u, 1u, 5u, m), "raton: boton sin cambio no emite");

	// Clamp de la posicion absoluta.
	MouseProducer c;
	Msg cm {};
	(void)c.update(0u, 0u, 0u, 1u, cm);
	c.x = 318;
	(void)c.update(10u, 0u, 0u, 2u, cm); // dx=+10 -> 328 -> clamp 319
	check(c.x == 319, "raton: clamp al maximo");
}

} // namespace

int main() {
	test_joy();
	test_pad();
	test_mouse();

	if (failures == 0) {
		std::printf("OK: productores de entrada (joystick, gamepad, raton) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
