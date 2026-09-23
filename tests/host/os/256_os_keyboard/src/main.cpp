// ============================================================================
// Test HOST-256: productor de teclado del mini-SO (eng/os/input.hpp).
// ============================================================================
//
// Valida el bit-reverse del scancode de la CIA, la distincion down/up (bit 7) y el seguimiento de
// modificadores (Shift/Ctrl/Alt/Amiga).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/os/256_os_keyboard

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

void test_reverse() {
	check(KeyProducer::reverse_bits7(0x60u) == 0x03u, "reverse 0x60 = 0x03");
	check(KeyProducer::reverse_bits7(0x03u) == 0x60u, "reverse 0x03 = 0x60");
	check(KeyProducer::reverse_bits7(0x01u) == 0x40u, "reverse 0x01 = 0x40");
	check(KeyProducer::reverse_bits7(0x00u) == 0x00u, "reverse 0x00 = 0x00");
}

void test_key() {
	KeyProducer k;
	Msg m {};
	const u8 raw_a = KeyProducer::reverse_bits7(0x20u); // 'A' pulsada
	check(k.update(raw_a, 1u, m) && m.type == MsgType::KeyDown && m.payload.key.code == 0x20u,
	      "A down");
	check(k.update(static_cast<u8>(raw_a | 0x80u), 2u, m) && m.type == MsgType::KeyUp &&
	      m.payload.key.code == 0x20u, "A up");

	const u8 shift = KeyProducer::reverse_bits7(0x60u);
	check(k.update(shift, 3u, m) && (m.payload.key.qual & kQualShift) != 0u, "Shift down");
	check(k.update(static_cast<u8>(shift | 0x80u), 4u, m) && (m.payload.key.qual & kQualShift) == 0u,
	      "Shift up");

	// 'A' con Shift pulsado: el modificador viaja en el payload.
	(void)k.update(shift, 5u, m);
	(void)k.update(raw_a, 6u, m);
	check((m.payload.key.qual & kQualShift) != 0u, "A con Shift");

	const u8 ctrl = KeyProducer::reverse_bits7(0x63u);
	(void)k.update(ctrl, 7u, m);
	check((m.payload.key.qual & kQualCtrl) != 0u, "Ctrl down");
}

} // namespace

int main() {
	test_reverse();
	test_key();

	if (failures == 0) {
		std::printf("OK: teclado (bit-reverse, down/up, modificadores) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
