// ============================================================================
// Test HOST-147: libro de aperturas y tablas de finales sobre bloques
// ============================================================================
//
// TUTORIAL. El conocimiento empaquetado tiene dos formas tipicas:
//
//   * LIBRO DE APERTURAS: entradas ordenadas por clave Zobrist; se consulta por
//     busqueda binaria y devuelve jugada + puntuacion + id de nombre de apertura.
//     Formato de 12 B: key u32 | move u32 | score s16 | name_id u16.
//
//   * TABLAS DE FINALES: entradas ordenadas por clave; devuelven valor y distancia
//     a la conversion. Formato de 8 B: key u32 | value s16 | dtm u8 | reservado.
//
// AMBOS SE LEEN BYTE A BYTE (`read_book_entry`/`read_endgame_entry`), nunca con
// `reinterpret_cast` a struct: en 68000 el acceso desalineado produce un bus error.
// El test comprueba el round-trip struct -> bytes -> bloque -> struct.
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/147_board_knowledge

#include <cstdio>
#include <cstring>

#include <eng/board/knowledge/book.hpp>
#include <eng/board/knowledge/cache.hpp>
#include <eng/board/knowledge/endgame_tables.hpp>

namespace {

using namespace eng::board;
using eng::u8;
using eng::u32;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_book_builder_and_probe() {
	BookEntry storage[4] {};
	BookBuilder builder {eng::Span<BookEntry> {storage, 4u}};
	// Se anaden fuera de orden para que `finalize` demuestre el ordenado.
	check(builder.add(100u, make_move(0u, 16u), 10, 0u), "libro: agrega 100");
	check(builder.add(50u, make_move(1u, 17u), 20, 1u), "libro: agrega 50");
	check(builder.add(75u, make_move(2u, 18u), 30, 2u), "libro: agrega 75");
	builder.finalize();
	check(builder.size() == 3u, "libro: 3 entradas");
	check(builder.entries()[0].key == 50u && builder.entries()[2].key == 100u,
	      "libro: ordenado por clave");

	const BookProbe found = probe_book(builder.entries(), 75u);
	check(found.found && found.move == make_move(2u, 18u) && found.score == 30 &&
	          found.name_id == 2u,
	      "libro: encuentra la clave 75");
	check(!probe_book(builder.entries(), 999u).found, "libro: clave ausente");
}

void test_book_names() {
	const char pool[] = "Ruy Lopez\0Sicilian\0Queen's Gambit\0";
	const eng::Span<const char> names {pool, sizeof(pool)};
	check(std::strcmp(book_name(names, 0u), "Ruy Lopez") == 0, "nombres: id 0");
	check(std::strcmp(book_name(names, 1u), "Sicilian") == 0, "nombres: id 1");
	check(std::strcmp(book_name(names, 2u), "Queen's Gambit") == 0, "nombres: id 2");
}

void test_book_roundtrip_through_block() {
	const BookEntry original[3] = {
	    {0x11111111u, make_move(0u, 16u), 5, 0u},
	    {0x22222222u, make_move(1u, 17u), -7, 1u},
	    {0x33333333u, make_move(2u, 18u), 9, 2u},
	};
	eng::u8 block_bytes[3u * 12u] = {};
	for (u32 i = 0u; i < 3u; ++i) {
		write_book_entry(block_bytes + i * 12u, original[i]);
	}

	RamBlockSource ram {eng::Span<const u8> {block_bytes, sizeof(block_bytes)}, sizeof(block_bytes)};
	BlockCache<sizeof(block_bytes), 2u> cache {ram.source()};
	const eng::Span<const u8> block = cache.get(0u);
	check(block.size() == sizeof(block_bytes), "round-trip: bloque completo");

	bool same = true;
	for (u32 i = 0u; i < 3u; ++i) {
		const BookEntry entry = read_book_entry(block.data() + i * 12u);
		if (entry.key != original[i].key || entry.move != original[i].move ||
		    entry.score != original[i].score || entry.name_id != original[i].name_id) {
			same = false;
		}
	}
	check(same, "round-trip: struct -> bytes -> bloque -> struct identico");
}

void test_endgame_table() {
	EndgameTableEntry storage[4] {};
	EndgameTableBuilder builder {eng::Span<EndgameTableEntry> {storage, 4u}};
	check(builder.add(0xabcd0001u, 30000, 3u), "finales: agrega ganada");
	check(builder.add(0xabcd0000u, 0, 0u), "finales: agrega tablas");
	builder.finalize();

	const EndgameTableProbe win = probe_endgame_table(builder.entries(), 0xabcd0001u);
	check(win.found && win.value == 30000 && win.dtm == 3u, "finales: encuentra ganada");
	const EndgameTableProbe draw = probe_endgame_table(builder.entries(), 0xabcd0000u);
	check(draw.found && draw.value == 0, "finales: encuentra tablas");
	check(!probe_endgame_table(builder.entries(), 0xdeadbeefu).found, "finales: ausente");

	// Round-trip de una entrada por bytes.
	eng::u8 bytes[8] = {};
	write_endgame_entry(bytes, EndgameTableEntry {0xabcd0001u, -1234, 7u, 0u});
	const EndgameTableEntry back = read_endgame_entry(bytes);
	check(back.key == 0xabcd0001u && back.value == -1234 && back.dtm == 7u,
	      "finales: round-trip de entrada");
}

} // namespace

int main() {
	std::printf("eng::board conocimiento:\n");
	test_book_builder_and_probe();
	test_book_names();
	test_book_roundtrip_through_block();
	test_endgame_table();

	if (g_fail == 0u) {
		std::printf("OK: conocimiento (libro, nombres, round-trip por bloque, tablas de finales)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
