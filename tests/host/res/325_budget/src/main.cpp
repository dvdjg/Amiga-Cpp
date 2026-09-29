// ============================================================================
// Test HOST-325: presupuesto de memoria (eng::res::Budget).
// ============================================================================
//
// Respalda `eng/res/budget.hpp`: la vista de solo lectura sobre los **bancos** (`MemoryManager`)
// para decidir si un recurso cabe antes de pedirlo. El presupuesto sale del **pool del banco**
// (reservas reales), no de la arena. Comprueba `used`/`remaining`/`capacity` y
// `can_fit`/`can_fit_chip`/`can_fit_slow`, y que un Budget sin memoria ligada no es válido.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/325_budget

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/memory/memory_manager.hpp>
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

	// --- Presupuesto sobre bancos reales ------------------------------------
	{
		eng::u8 chip_buf[1024] {};
		eng::u8 slow_buf[256] {};
		eng::MemoryManager mm {};
		mm.configure(chip_buf, sizeof(chip_buf), slow_buf, sizeof(slow_buf), nullptr, 0u, 16u);
		const eng::res::Budget b {mm};

		check(b.valid(), "budget valido");
		check(b.capacity_chip() == 1024u, "capacidad Chip");
		check(b.used_chip() == 0u && b.remaining_chip() == 1024u, "Chip vacia");
		check(b.can_fit_chip(1024u), "cabe justo");
		check(!b.can_fit_chip(1025u), "no cabe por 1 byte");

		// Reserva real en el banco Chip (pool): el presupuesto la refleja.
		const auto blk = mm.chip().reserve<eng::PlaneTag>(256u, 2u);
		check(blk.valid(), "reserva Chip");
		check(b.used_chip() == 256u, "usado refleja la reserva");
		check(b.remaining_chip() == 768u, "libre tras la reserva");
		check(b.can_fit(256u, eng::MemoryKind::Chip), "can_fit Chip");
		check(!b.can_fit(769u, eng::MemoryKind::Chip), "can_fit Chip rechaza");

		// Slow tiene su propio banco; Fast sin banco cae a Slow (como `fast_or_slow`).
		check(b.can_fit_slow(256u) && !b.can_fit_slow(257u), "can_fit Slow");
		check(b.can_fit(200u, eng::MemoryKind::Fast), "Fast sin banco -> Slow");
		check(!b.can_fit(300u, eng::MemoryKind::Fast), "Fast rechaza si Slow no cabe");

		// `memory()` da acceso al MemoryManager observado (para reservar o snapshot).
		check(&b.memory() == &mm, "memory() observa el mismo gestor");
		check(mm.chip().free_bytes() == b.remaining_chip(), "coherente con el banco");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Budget (used/remaining/capacity, can_fit Chip/Slow/Fast) validado.\n");
	return 0;
}
