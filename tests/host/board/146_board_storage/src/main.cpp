// ============================================================================
// Test HOST-146: fuente de bloques y cache LRU (eng::board::storage/knowledge)
// ============================================================================
//
// TUTORIAL. El conocimiento (libro de aperturas, tablas de finales) no cabe en RAM:
// vive en almacenamiento externo y se pide por bloques. La fuente es un **tipo** que
// cumple el concept `BlockSource` (`block_size()`, `block_count()`,
// `fetch(id, Span<u8>)`): no hay punteros a funcion ni `void*`. El contrato de tres
// estados es el mismo del streaming del engine:
//
//   Ready   -> el bloque se escribio en dst (usar)
//   Empty   -> el bloque no existe (no reintentar)
//   Pending -> lectura asincrona no lista (reintentar luego)
//
// Aqui se usa la fuente en RAM (`RamBlockSource`), ya funcional; los backends de
// disquete Amiga (trackloader) y de sistema de archivos del PC se enchufan como
// otros tipos que cumplen el mismo contrato.
//
// `BlockCache<Source, BlockSize, Capacity>` guarda los ultimos bloques para no
// repetir la peticion (que en disquete cuesta un seek). La RAM que ocupa es
// exactamente Capacity*BlockSize.
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/146_board_storage

#include <cstdio>

#include <eng/board/knowledge/cache.hpp>
#include <eng/board/storage/block_source.hpp>

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

// Tres bloques de 8 bytes, contiguos en RAM.
const eng::u8 g_blob[24] = {
    1u,  2u,  3u,  4u,  5u,  6u,  7u,  8u,  // bloque 0
    9u,  10u, 11u, 12u, 13u, 14u, 15u, 16u, // bloque 1
    17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u  // bloque 2
};

void test_ram_source() {
	const RamBlockSource ram {eng::Span<const u8> {g_blob, 24u}, 8u};
	check(ram.block_count() == 3u, "ram: 24/8 = 3 bloques");

	eng::u8 buffer[8] = {};
	check(ram.fetch(1u, eng::Span<u8> {buffer, 8u}) == BlockStatus::Ready,
	      "ram: bloque interno Ready");
	check(buffer[0] == 9u && buffer[7] == 16u, "ram: datos del bloque 1");
	check(ram.fetch(3u, eng::Span<u8> {buffer, 8u}) == BlockStatus::Empty,
	      "ram: fuera de rango Empty");
}

void test_cache_hits_and_eviction() {
	const RamBlockSource ram {eng::Span<const u8> {g_blob, 24u}, 8u};
	BlockCache<RamBlockSource, 8u, 2u> cache {ram};

	const eng::Span<const u8> b0 = cache.get(0u);
	check(b0.size() == 8u && b0[0] == 1u, "cache: lee el bloque 0");
	check(cache.misses() == 1u && cache.hits() == 0u, "cache: primer acceso es fallo");

	const eng::Span<const u8> b1 = cache.get(1u);
	check(b1.size() == 8u && b1[0] == 9u, "cache: lee el bloque 1");
	check(cache.misses() == 2u, "cache: segundo bloque tambien falla");

	const eng::Span<const u8> b0_again = cache.get(0u);
	check(b0_again[0] == 1u && cache.hits() == 1u, "cache: el bloque 0 ahora acierta");
	check(cache.resident() == 2u, "cache: dos bloques residentes");

	// Capacidad 2 llena: pedir el 2 desaloja el menos reciente (el 1).
	const eng::Span<const u8> b2 = cache.get(2u);
	check(b2.size() == 8u && b2[0] == 17u, "cache: lee el bloque 2");
	check(cache.evictions() == 1u, "cache: hubo una expulsion");
	check(cache.resident() == 2u, "cache: sigue llena");

	const eng::Span<const u8> b1_evicted = cache.get(1u);
	check(b1_evicted.size() == 8u && b1_evicted[0] == 9u, "cache: el 1 se recarga");
	check(cache.misses() >= 3u, "cache: recargar cuenta como fallo");
}

void test_absent_not_cached() {
	const RamBlockSource ram {eng::Span<const u8> {g_blob, 24u}, 8u};
	BlockCache<RamBlockSource, 8u, 2u> cache {ram};
	check(cache.get(9u).empty(), "ausente: devuelve vista vacia");
	check(cache.resident() == 0u, "ausente: no se cachea");
	check(cache.get(9u).empty(), "ausente: se reintenta en cada peticion");
}

void test_invalid_source() {
	const NullBlockSource bad {};
	BlockCache<NullBlockSource, 8u, 2u> cache {bad};
	check(cache.get(0u).empty(), "sin backend: devuelve vacio (Empty)");
}

} // namespace

int main() {
	std::printf("eng::board storage:\n");
	test_ram_source();
	test_cache_hits_and_eviction();
	test_absent_not_cached();
	test_invalid_source();

	if (g_fail == 0u) {
		std::printf("OK: storage (BlockSource 3 estados, cache LRU, ausentes e invalidos)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
