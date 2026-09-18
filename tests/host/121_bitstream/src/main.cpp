// ============================================================================
// Test HOST-121: campos de bits (BitWriter / BitReader, LSB-first)
// ============================================================================
//
// Valida `engine/include/eng/core/util/bitstream.hpp`:
//
//   1) Round-trip de campos de 3/16/1/32 bits.
//   2) Capacidad del buffer y `full`.
//   3) Lectura mas alla del final (falla sin consumir).
//   4) Anchos invalidos (0 y >32).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/121_bitstream

#include <cstdio>

#include <eng/core/util/bitstream.hpp>

namespace {

using eng::u8;
using eng::u32;
using eng::usize;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_roundtrip() {
	u8 buf[16] {};
	eng::util::BitWriter bw {buf};
	check(bw.write(5u, 3u), "bits: escribe 3 bits");
	check(bw.write(0xABCDu, 16u), "bits: escribe 16 bits");
	check(bw.write_bool(true), "bits: escribe un bool");
	check(bw.write(0xFFFFFFFFu, 32u), "bits: escribe 32 bits");
	check(bw.bit_count() == 52u, "bits: 52 bits escritos");
	check(bw.byte_count() == 7u, "bits: 7 bytes ocupados");

	eng::util::BitReader br {eng::Span<const u8> {buf, bw.byte_count()}};
	u32 a = 0;
	u32 b = 0;
	bool c = false;
	u32 d = 0;
	check(br.read(3u, a) && a == 5u, "bits: lee 3 bits");
	check(br.read(16u, b) && b == 0xABCDu, "bits: lee 16 bits");
	check(br.read_bool(c) && c, "bits: lee el bool");
	check(br.read(32u, d) && d == 0xFFFFFFFFu, "bits: lee 32 bits");
	check(br.remaining() == 4u, "bits: quedan los 4 bits de relleno");
}

void test_capacity() {
	u8 buf[1] {};
	eng::util::BitWriter bw {buf};
	check(bw.write(0xFFu, 8u), "bits: 8 bits caben en 1 byte");
	check(!bw.write(1u, 1u), "bits: el noveno bit no cabe");
	check(bw.full() && bw.bit_count() == 8u, "bits: buffer lleno");
}

void test_read_past_end() {
	u8 buf[1] = {0xFFu};
	eng::util::BitReader br {eng::Span<const u8> {buf, 1u}};
	u32 v = 0;
	check(br.read(8u, v) && v == 0xFFu, "bits: lee el byte entero");
	check(!br.read(1u, v), "bits: leer pasado el final falla");
	check(br.remaining() == 0u, "bits: sin bits restantes");
}

void test_invalid_width() {
	u8 buf[4] {};
	eng::util::BitWriter bw {buf};
	check(!bw.write(0u, 0u), "bits: ancho 0 invalido");
	check(!bw.write(0u, 33u), "bits: ancho > 32 invalido");
}

} // namespace

int main() {
	std::printf("BitStream:\n");
	test_roundtrip();
	test_capacity();
	test_read_past_end();
	test_invalid_width();

	if (g_fail == 0u) {
		std::printf("OK: BitStream (round-trip, capacidad, fin de buffer, anchos)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
