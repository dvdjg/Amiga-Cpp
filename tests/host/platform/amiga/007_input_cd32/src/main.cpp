// ============================================================================
// Test HOST-007: decodificación de botones CD32 (eng::amiga::decode_cd32_buttons).
// ============================================================================
//
// Valida en host la parte pura del protocolo CD32: convertir el flujo serie de
// 9 bits (7 botones + 2 de identificación) en una máscara de botones, y degradar
// a joystick de 1/2 botones cuando falta la firma CD32. Mismo mapeo que Sevgi.

#include <cstdio>

#include <eng/core/types.hpp>
#include <eng/platform/input_poll.hpp>

namespace {

using eng::amiga::decode_cd32_buttons;
using eng::amiga::kCd32Blue;
using eng::amiga::kCd32Red;
using eng::amiga::kCd32Yellow;
using eng::amiga::kCd32Green;
using eng::amiga::kCd32Forward;
using eng::amiga::kCd32Reverse;
using eng::amiga::kCd32Play;

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

void test_cd32_signature() {
	std::printf("cd32: firma valida (bit8=1, bit7=0) conserva los 7 botones\n");
	// Firma 0x100 + rojo(bit1) + amarillo(bit2) = 0x106.
	CHECK(decode_cd32_buttons(0x106u) == (kCd32Red | kCd32Yellow));
	// Todos los botones: 0x7f (bits 0..6) + firma 0x100.
	CHECK(decode_cd32_buttons(0x17fu) == 0x7fu);
}

void test_dumb_joystick() {
	std::printf("cd32: sin firma degrada a joystick de 1/2 botones\n");
	// Sin bits de ID: solo azul(bit0) y rojo(bit1) sobreviven.
	CHECK(decode_cd32_buttons(0x03u) == (kCd32Blue | kCd32Red));
	// Solo rojo (bit1).
	CHECK(decode_cd32_buttons(0x02u) == kCd32Red);
	// Solo azul (bit0).
	CHECK(decode_cd32_buttons(0x01u) == kCd32Blue);
	// Bits extra (amarillo/verde/...) se descartan sin firma: 0x7f -> azul+rojo.
	CHECK(decode_cd32_buttons(0x7fu) == (kCd32Blue | kCd32Red));
}

void test_individual_buttons() {
	std::printf("cd32: botones individuales con firma\n");
	CHECK(decode_cd32_buttons(0x100u | kCd32Blue) == kCd32Blue);
	CHECK(decode_cd32_buttons(0x100u | kCd32Red) == kCd32Red);
	CHECK(decode_cd32_buttons(0x100u | kCd32Yellow) == kCd32Yellow);
	CHECK(decode_cd32_buttons(0x100u | kCd32Green) == kCd32Green);
	CHECK(decode_cd32_buttons(0x100u | kCd32Forward) == kCd32Forward);
	CHECK(decode_cd32_buttons(0x100u | kCd32Reverse) == kCd32Reverse);
	CHECK(decode_cd32_buttons(0x100u | kCd32Play) == kCd32Play);
}

} // namespace

int main() {
	std::printf("Test HOST-007 input_cd32\n");
	std::printf("========================\n");

	test_cd32_signature();
	test_dumb_joystick();
	test_individual_buttons();

	if (g_failures == 0) {
		std::printf("OK: decodificacion CD32 validada (serie -> botones).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
