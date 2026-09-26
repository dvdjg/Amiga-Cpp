// ============================================================================
// Test HOST-340: pool de bloques con free (eng::res::ChipPool) - F7.6.
// ============================================================================
//
// Respalda `eng/res/chip_pool.hpp`: asignador *first-fit* con fusion de huecos sobre un buffer
// (sin heap), para quien recicla memoria (memoria reutilizable). Comprueba
// alineacion, free, fusion y rechazo cuando no cabe.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/340_chip_pool

#include <cstdio>

#include <eng/res/chip_pool.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-340 chip_pool ==\n");

	eng::u8 buf[1024] {};
	eng::res::ChipPool pool {buf, sizeof(buf), 16u};

	check(pool.capacity() == 1024u && pool.free_bytes() == 1024u, "pool vacio");

	void* a = pool.alloc(100u); // 100 -> 112 alineado
	check(a != nullptr, "alloc a");
	check(reinterpret_cast<eng::uintptr>(a) % 16u == 0u, "a alineado a 16");
	check(pool.free_bytes() == 1024u - 112u, "libre tras a");

	void* b = pool.alloc(256u);
	check(b != nullptr && b != a, "alloc b distinto");
	check(pool.free_bytes() == 1024u - 368u, "libre tras a+b");

	pool.free(a);
	check(pool.free_bytes() == 1024u - 256u, "libre tras free(a)");

	pool.free(b);
	check(pool.free_bytes() == 1024u, "fusion: todo libre tras free(b)");
	check(pool.block_count() == 1u, "fusion de huecos contiguos");

	check(pool.alloc(2000u) == nullptr, "no cabe -> nullptr");

	eng::res::ChipPool empty {};
	check(empty.alloc(16u) == nullptr, "pool por defecto -> nullptr");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: ChipPool (first-fit, free, fusion) validado.\n");
	return 0;
}
