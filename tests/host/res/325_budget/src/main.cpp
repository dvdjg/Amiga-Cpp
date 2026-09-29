// ============================================================================
// Test HOST-325: presupuesto de memoria (eng::res::Budget).
// ============================================================================
//
// Respalda `eng/res/budget.hpp`: la vista de solo lectura sobre las arenas del engine para
// decidir si un recurso cabe antes de pedirlo. Comprueba `used`/`remaining`/`capacity` y
// `can_fit`/`can_fit_chip`/`can_fit_slow` sobre un `MemorySystem` real, y que un Budget sin
// memoria ligada no es válido.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/325_budget

#include <cstdio>

#include <eng/memory/arena.hpp>
#include <eng/res/budget.hpp>

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
	std::printf("== HOST-325 budget ==\n");

	// --- Budget sin memoria ligada ------------------------------------------
	{
		const eng::res::Budget none {};
		check(!none.valid(), "budget sin memoria no valido");
	}

	// --- Presupuesto sobre un MemorySystem real ------------------------------
	{
		eng::u8 chip_buf[1024] {};
		eng::u8 slow_buf[256] {};
		eng::MemorySystem ms {
			eng::ChipArena {chip_buf, sizeof(chip_buf), eng::MemoryKind::Chip},
			eng::LinearArena {slow_buf, sizeof(slow_buf), eng::MemoryKind::Slow},
			eng::ChipArena {},
		};
		const eng::res::Budget b {ms};

		check(b.valid(), "budget valido");
		check(b.capacity_chip() == 1024u, "capacidad Chip");
		check(b.used_chip() == 0u && b.remaining_chip() == 1024u, "Chip vacia");
		check(b.can_fit_chip(1024u), "cabe justo");
		check(!b.can_fit_chip(1025u), "no cabe por 1 byte");

		const eng::MemoryBlock blk = ms.chip.allocate(256u, 2u);
		check(blk.valid(), "reserva Chip");
		check(b.used_chip() == 256u, "usado refleja la reserva");
		check(b.remaining_chip() == 768u, "libre tras la reserva");
		check(b.can_fit(256u, eng::MemoryKind::Chip), "can_fit Chip");
		check(!b.can_fit(769u, eng::MemoryKind::Chip), "can_fit Chip rechaza");

		// Slow se sirve de su propia arena; Fast se mapea a Slow.
		check(b.can_fit_slow(256u) && !b.can_fit_slow(257u), "can_fit Slow");
		check(b.can_fit(200u, eng::MemoryKind::Fast), "Fast -> arena Slow");
		check(!b.can_fit(300u, eng::MemoryKind::Fast), "Fast rechaza si Slow no cabe");

		// `memory()` da acceso al MemorySystem observado (para reservar o snapshot).
		check(&b.memory() == &ms, "memory() observa el mismo sistema");
		check(ms.chip.remaining() == b.remaining_chip(), "coherente con la arena");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Budget (used/remaining/capacity, can_fit Chip/Slow/Fast) validado.\n");
	return 0;
}
