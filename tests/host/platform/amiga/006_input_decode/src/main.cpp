// ============================================================================
// Test HOST-006: decodificación del joystick (eng::amiga::decode_joystick).
// ============================================================================
//
// Valida en host la única pieza pura del backend de entrada: la decodificación
// de los bits del contador JOYxDAT de Denise en direcciones.
//
// El contador es un código de GRAY de 2 bits por eje (AHRM cap. 8 y Ap. A;
// mismo mapeo que los motores ACE y Sevgi):
//
//   eje Y (bits 8-9):  00=reposo  01(0x0100)=arriba  11(0x0300)=izquierda  10(0x0200)=arriba+izq
//   eje X (bits 0-1):  00=reposo  01(0x0001)=abajo   11(0x0003)=derecha   10(0x0002)=abajo+der
//
//   right = bit1  left = bit9  up = bit9^bit8  down = bit1^bit0
//
// Comprobaciones:
//   1) Reposo (sin conmutadores) → sin dirección.
//   2) Cada dirección pura.
//   3) Diagonales.
//   4) El fuego no viene de JOYxDAT (lo gestiona CIAAPRA aparte).

#include <cstdio>

#include <eng/core/types.hpp>
#include <eng/platform/input_poll.hpp>

namespace {

using eng::amiga::decode_joystick;
using eng::amiga::kJoyDown;
using eng::amiga::kJoyLeft;
using eng::amiga::kJoyRight;
using eng::amiga::kJoyUp;

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

void test_idle() {
	std::printf("input: joystick en reposo sin direcciones\n");
	CHECK(decode_joystick(0x0000u) == 0u);
}

void test_single_directions() {
	std::printf("input: direcciones puras\n");
	CHECK(decode_joystick(0x0100u) == kJoyUp);    // bit8
	CHECK(decode_joystick(0x0001u) == kJoyDown);  // bit0
	CHECK(decode_joystick(0x0300u) == kJoyLeft);  // bit8+bit9
	CHECK(decode_joystick(0x0003u) == kJoyRight); // bit0+bit1
}

void test_diagonals() {
	std::printf("input: diagonales\n");
	// Arriba + izquierda = 0x0200 (bit9); arriba + derecha = 0x0103.
	CHECK(decode_joystick(0x0200u) == (kJoyUp | kJoyLeft));
	CHECK(decode_joystick(0x0103u) == (kJoyUp | kJoyRight));
	CHECK(decode_joystick(0x0301u) == (kJoyDown | kJoyLeft));
	CHECK(decode_joystick(0x0002u) == (kJoyDown | kJoyRight));
}

void test_fire_is_cia() {
	std::printf("input: fuego no viene de JOYxDAT (lo gestiona CIAAPRA)\n");
	// JOYxDAT no contiene el fuego; decode_joystick nunca debe setearlo.
	CHECK((decode_joystick(0xFFFFu) & eng::amiga::kJoyFire) == 0u);
}

} // namespace

int main() {
	std::printf("Test HOST-006 input_decode\n");
	std::printf("==========================\n");

	test_idle();
	test_single_directions();
	test_diagonals();
	test_fire_is_cia();

	if (g_failures == 0) {
		std::printf("OK: decodificacion del joystick validada (JOYxDAT -> direcciones).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
