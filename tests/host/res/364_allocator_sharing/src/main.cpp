// ============================================================================
// Test HOST-364: asignador unico por medio (bancos enlazados a las arenas).
// ============================================================================
//
// Respalda `BlockPool::configure_backing` y `MemoryManager::configure_backing`: los bancos de
// Chip/Slow **delegan en las arenas del `MemorySystem`** (mismo buffer y **mismo cursor**), de modo
// que reservar desde la arena y desde el banco **no se solapa**. Era el bug del backend Amiga:
// `configure_memory` entregaba el mismo buffer a dos asignadores independientes y `res::load`
// pisaba el bitmap/copperlist de `compose`.
//
// La variante `configure(base, ...)` (buffers propios) sigue viva para tests host que no comparten.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/364_allocator_sharing

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/memory/arena.hpp>
#include <eng/memory/memory_manager.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

alignas(16) eng::u8 g_chip[64u * 1024u];
alignas(16) eng::u8 g_slow[16u * 1024u];

int main() {
	std::printf("== HOST-364 allocator sharing ==\n");

	// --- 1) Bancos enlazados a las arenas: un unico cursor, sin solape --------------
	eng::ChipArena chip {g_chip, sizeof(g_chip), eng::MemoryKind::Chip};
	eng::LinearArena slow {g_slow, sizeof(g_slow), eng::MemoryKind::Slow};
	eng::MemoryManager mm {};
	check(mm.configure_backing(chip, slow, nullptr, 0u), "configure_backing ok");

	const auto arena_blk = chip.allocate_block<eng::PlaneTag>(1024u, 16u);
	const auto bank_blk = mm.chip().reserve<eng::PlaneTag>(1024u, 16u);
	check(arena_blk.valid() && bank_blk.valid(), "reservas validas");
	check(arena_blk.data() != bank_blk.data(), "arena y banco NO comparten direccion (sin solape)");
	check(chip.used() == 2048u, "el cursor de la arena avanzo con la reserva del banco");
	check(mm.chip().capacity() == chip.capacity(), "capacity del banco = arena");
	check(mm.chip().free_bytes() == chip.remaining(), "free_bytes del banco = remaining de la arena");
	check(mm.chip().kind() == eng::MemoryKind::Chip, "kind del banco = Chip");

	// El banco ya no recicla (la arena es *bump*): `release` no devuelve memoria al cursor.
	const eng::u32 used_before = chip.used();
	mm.chip().release(bank_blk);
	check(chip.used() == used_before, "release del banco no rebobina el cursor de la arena");

	// --- 2) `configure(base, ...)` con buffers propios sigue funcionando -------------
	alignas(16) static eng::u8 own_buf[4096];
	eng::MemoryManager mm2 {};
	check(mm2.configure(own_buf, sizeof(own_buf), nullptr, 0u, nullptr, 0u, 16u),
	      "configure(base) ok");
	const auto own = mm2.chip().reserve<eng::PlaneTag>(256u);
	check(own.valid(), "configure(base) reserva");
	check(mm2.chip().capacity() == sizeof(own_buf), "capacity del banco propio");

	std::printf(g_fail != 0 ? "[FAIL] %d\n" : "OK: asignador unico por medio (sin solape)\n",
		    g_fail);
	return g_fail != 0 ? 1 : 0;
}
