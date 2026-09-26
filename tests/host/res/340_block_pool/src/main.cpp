// ============================================================================
// Test HOST-340: pool de bloques con free (eng::BlockPool) - generico por medio.
// ============================================================================
//
// Respalda `eng/memory/block_pool.hpp`: asignador *first-fit* con fusion de huecos sobre un buffer
// (sin heap). Generico: opera sobre cualquier medio y lleva su `MemoryKind` en cada reserva.
// Comprueba alineacion, free, fusion, el medio en `MemoryBlock`/`Block<Tag>` y rechazo si no cabe.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/340_block_pool

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/memory/block_pool.hpp>

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
	std::printf("== HOST-340 block_pool ==\n");

	eng::u8 buf[1024] {};
	eng::BlockPool pool {buf, sizeof(buf), eng::MemoryKind::Chip, 16u};

	check(pool.capacity() == 1024u && pool.free_bytes() == 1024u, "pool vacio");
	check(pool.kind() == eng::MemoryKind::Chip, "medio Chip");

	const eng::MemoryBlock a = pool.allocate(100u); // 100 -> 112 alineado
	check(a.valid(), "alloc a");
	check(a.kind == eng::MemoryKind::Chip, "bloque con el medio del pool");
	check(reinterpret_cast<eng::uintptr>(a.data) % 16u == 0u, "a alineado a 16");
	check(pool.free_bytes() == 1024u - 112u, "libre tras a");

	// Reserva tipada (mismo API que LinearArena).
	const auto b = pool.allocate_block<eng::PlaneTag>(256u);
	check(b.valid() && b.kind == eng::MemoryKind::Chip, "allocate_block<PlaneTag> con medio");
	check(pool.free_bytes() == 1024u - 368u, "libre tras a+b");

	pool.free(a.data);
	check(pool.free_bytes() == 1024u - 256u, "libre tras free(a)");

	pool.free(b.view.data());
	check(pool.free_bytes() == 1024u, "fusion: todo libre tras free(b)");
	check(pool.block_count() == 1u, "fusion de huecos contiguos");

	check(!pool.allocate(2000u).valid(), "no cabe -> invalido");

	// El mismo pool sirve para cualquier medio (Fast): generico, no una clase por banco.
	eng::BlockPool fast {buf, sizeof(buf), eng::MemoryKind::Fast};
	check(fast.allocate(16u).kind == eng::MemoryKind::Fast, "mismo tipo, medio Fast");

	eng::BlockPool empty {};
	check(!empty.allocate(16u).valid(), "pool por defecto -> invalido");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: BlockPool (first-fit, free, fusion, MemoryKind) validado.\n");
	return 0;
}
