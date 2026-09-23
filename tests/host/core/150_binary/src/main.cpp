// ============================================================================
// Test HOST-150: lectura/escritura binaria segura (eng::util::ByteReader/Writer)
// ============================================================================
//
// TUTORIAL. `eng/core/util/binary.hpp` aporta cursores little-endian sobre `Span`
// que comprueban limites en cada operacion. Sustituyen a `*ptr++`, `memcpy` y al
// `reinterpret_cast` a structs: en 68000 un acceso desalineado produce bus error, y
// un desbordamiento silencioso corrompe memoria. Aqui no hay punteros ni aritmetica
// de punteros: cada campo se lee/escribe con `read_uXX`/`write_uXX` y el cursor no
// avanza si no hay bytes.
//
// Se usa para serializar el libro de aperturas, las tablas de finales y formatos de
// assets. El test verifica round-trip, limites exactos y copias de bloques.
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/150_binary

#include <cstdio>

#include <eng/core/util/binary.hpp>

namespace {

using eng::u8;
using eng::u16;
using eng::u32;
using eng::s16;
using eng::s32;
using eng::util::ByteReader;
using eng::util::ByteWriter;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_roundtrip_integers() {
	eng::u8 buffer[16] = {};
	ByteWriter writer {eng::Span<u8> {buffer, sizeof(buffer)}};
	check(writer.write_u8(0x12u), "writer: u8");
	check(writer.write_u16(0x3456u), "writer: u16");
	check(writer.write_u32(0x789abcdeu), "writer: u32");
	check(writer.write_s16(-1234), "writer: s16");
	check(writer.write_s32(-1234567), "writer: s32");
	check(writer.position() == 1u + 2u + 4u + 2u + 4u, "writer: posicion acumulada");
	check(writer.remaining() == sizeof(buffer) - writer.position(), "writer: restante");

	ByteReader reader {eng::Span<const u8> {buffer, sizeof(buffer)}};
	u8 a = 0u;
	u16 b = 0u;
	u32 c = 0u;
	s16 d = 0;
	s32 e = 0;
	check(reader.read_u8(a) && a == 0x12u, "reader: u8");
	check(reader.read_u16(b) && b == 0x3456u, "reader: u16 little-endian");
	check(reader.read_u32(c) && c == 0x789abcdeu, "reader: u32 little-endian");
	check(reader.read_s16(d) && d == -1234, "reader: s16");
	check(reader.read_s32(e) && e == -1234567, "reader: s32");
	check(reader.position() == 13u && reader.remaining() == 3u, "reader: limites exactos");
}

void test_bounds() {
	eng::u8 tiny[2] = {};
	ByteWriter writer {eng::Span<u8> {tiny, sizeof(tiny)}};
	check(writer.write_u16(0x1111u), "bounds: caben 2 bytes");
	check(!writer.write_u8(0u), "bounds: no cabe un tercer byte");
	check(writer.position() == 2u, "bounds: el cursor no avanza al fallar");

	ByteReader reader {eng::Span<const u8> {tiny, sizeof(tiny)}};
	u32 value = 0u;
	check(!reader.read_u32(value), "bounds: no hay 4 bytes");
	check(reader.position() == 0u, "bounds: el lector no avanza al fallar");
}

void test_block_copy() {
	eng::u8 source[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
	eng::u8 destination[4] = {};
	ByteReader reader {eng::Span<const u8> {source, sizeof(source)}};
	check(reader.read_into(eng::Span<u8> {destination, sizeof(destination)}), "copia: 4 bytes");
	check(destination[0] == 1u && destination[3] == 4u, "copia: contenido");

	eng::Span<const u8> view {};
	check(reader.read_view(2u, view), "vista: 2 bytes");
	check(view.size() == 2u && view[0] == 5u && view[1] == 6u, "vista: contenido");
	check(reader.position() == 6u, "vista: cursor avanzado");

	ByteWriter writer {eng::Span<u8> {destination, sizeof(destination)}};
	check(writer.write_bytes(view), "write_bytes: caben 2");
	check(destination[0] == 5u && destination[1] == 6u, "write_bytes: contenido");
}

} // namespace

int main() {
	std::printf("eng::util::binary:\n");
	test_roundtrip_integers();
	test_bounds();
	test_block_copy();

	if (g_fail == 0u) {
		std::printf("OK: binary (round-trip, little-endian, limites y copias)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
