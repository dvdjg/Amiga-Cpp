// ============================================================================
// Test HOST-151: E/S real de bloques desde un fichero del PC
// ============================================================================
//
// TUTORIAL. `FileBlockSource` (`eng/board/storage/file_block_source.hpp`) es la E/S
// real del lado host: lee bloques de un fichero con `std::FILE` y cumple el mismo
// contrato `BlockSource` que la fuente RAM o el futuro trackloader del Amiga.
//
// El test recorre la cadena completa del conocimiento en PC:
//
//   entradas -> bytes (ByteWriter) -> fichero -> FileBlockSource -> BlockCache
//            -> ByteReader -> BookEntry -> probe_book
//
// asi se valida que el libro empaquetado (p. ej. por `tools/board/pack-book.sh`) se
// sirve por bloques sin cargar todo en RAM y sin `reinterpret_cast`.
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/151_board_file

#include <cstdio>

#include <eng/board/knowledge/book.hpp>
#include <eng/board/knowledge/cache.hpp>
#include <eng/board/storage/file_block_source.hpp>
#include <eng/core/util/binary.hpp>

namespace {

using namespace eng::board;
using eng::u8;
using eng::u32;
using eng::util::ByteReader;
using eng::util::ByteWriter;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Escribe `data` en el primer destino escribible y devuelve su ruta (o nullptr).
const char* write_temp_file(const u8* data, eng::usize size, char* path_out, eng::usize path_cap) {
	const char* candidates[3] = {"out/tmp/host151_book.bin", "out/host151_book.bin",
	                             "host151_book.bin"};
	for (int i = 0; i < 3; ++i) {
		std::FILE* file = std::fopen(candidates[i], "wb");
		if (file == nullptr) {
			continue;
		}
		const bool ok = std::fwrite(data, 1u, size, file) == size;
		std::fclose(file);
		if (!ok) {
			continue;
		}
		eng::usize k = 0u;
		while (candidates[i][k] != '\0' && k + 1u < path_cap) {
			path_out[k] = candidates[i][k];
			++k;
		}
		path_out[k] = '\0';
		return path_out;
	}
	return nullptr;
}

void test_file_block_source() {
	const BookEntry original[3] = {
	    {0x11111111u, make_move(0u, 16u), 5, 0u},
	    {0x22222222u, make_move(1u, 17u), -7, 1u},
	    {0x33333333u, make_move(2u, 18u), 9, 2u},
	};
	u8 blob[3u * 12u] = {};
	{
		ByteWriter writer {eng::Span<u8> {blob, sizeof(blob)}};
		for (u32 i = 0u; i < 3u; ++i) {
			check(write_book_entry(writer, original[i]), "fichero: serializa entrada");
		}
	}

	char path[128] = {};
	check(write_temp_file(blob, sizeof(blob), path, sizeof(path)) != nullptr,
	      "fichero: se crea el temporal");
	if (path[0] == '\0') {
		return;
	}

	// Bloque = una entrada (12 B): el fichero tiene 3 bloques.
	FileBlockSource source;
	check(source.open(path, 12u), "file: abre el fichero");
	check(source.block_count() == 3u && source.block_size() == 12u, "file: geometria");

	FileBlockSource missing;
	source.close();
	check(!missing.open("no_existe_151.bin", 12u), "file: ruta inexistente falla");

	// Recorre los bloques a traves de la cache y reconstruye las entradas.
	FileBlockSource reader;
	check(reader.open(path, 12u), "file: reabre para leer");
	BlockCache<FileBlockSource, 12u, 2u> cache {reader};

	BookEntry recovered[3] = {};
	bool ok = true;
	for (u32 i = 0u; i < 3u; ++i) {
		const eng::Span<const u8> block = cache.get(i);
		if (block.size() != 12u) {
			ok = false;
			break;
		}
		ByteReader entry_reader {block};
		if (!read_book_entry(entry_reader, recovered[i])) {
			ok = false;
			break;
		}
	}
	check(ok, "file: lee los 3 bloques como entradas");
	check(recovered[2].key == original[2].key && recovered[2].move == original[2].move &&
	          recovered[2].score == original[2].score,
	      "file: contenido identico al empaquetado");

	const BookProbe probe = probe_book(eng::Span<const BookEntry> {recovered, 3u}, 0x22222222u);
	check(probe.found && probe.name_id == 1u, "file: probe_book encuentra la clave");
	check(cache.misses() >= 3u, "file: la cache registra los fallos de carga");

	std::remove(path);
}

} // namespace

int main() {
	std::printf("eng::board file source:\n");
	test_file_block_source();

	if (g_fail == 0u) {
		std::printf("OK: file source (empaquetar -> fichero -> bloques -> probe)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
