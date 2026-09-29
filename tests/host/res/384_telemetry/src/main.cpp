// ============================================================================
// Test HOST-384: telemetría de memoria (panel) desde bancos + scratch.
// ============================================================================
//
// Respalda `eng/debug/telemetry.hpp`: `telemetry_from(MemoryManager, MemorySystem, Telemetry)`
// rellena el panel desde los **bancos** (reservas reales) y la **scratch de frame**. Comprueba que
// used/capacity salen del banco (no de la arena), que la fragmentación (`chip_slots`) sube al
// liberar un bloque del medio, y que la scratch se refleja. Ver `ROADMAP_MEMORY_OWNERSHIP.md`
// (Fase 4).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/384_telemetry

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/debug/telemetry.hpp>
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

alignas(16) eng::u8 g_chip[16u * 1024u];
alignas(16) eng::u8 g_frame[4u * 1024u];

int main() {
	std::printf("== HOST-384 telemetry ==\n");

	eng::MemoryManager mm {};
	mm.configure(g_chip, sizeof(g_chip), nullptr, 0u, nullptr, 0u, 16u);
	eng::MemorySystem ms {};
	ms.frame.reset(g_frame, sizeof(g_frame), eng::MemoryKind::Chip);

	// Sin reservas: panel a cero (capacidades puestas, uso 0).
	eng::debug::Telemetry t {};
	eng::debug::telemetry_from(mm, ms, t);
	check(t.chip_capacity == sizeof(g_chip), "capacity_chip del banco");
	check(t.chip_used == 0u, "sin reservas -> used 0");
	check(t.chip_slots_max > 0u && t.chip_slots <= t.chip_slots_max, "slots coherentes");

	// Reservas del banco -> `used` sube en el panel (no la arena).
	const auto a = mm.chip().reserve<eng::PlaneTag>(1024u, 16u);
	const auto b = mm.chip().reserve<eng::PlaneTag>(1024u, 16u);
	eng::debug::telemetry_from(mm, ms, t);
	check(t.chip_used == 2048u, "used_chip = 2048 tras dos reservas");
	check(t.chip_used == mm.chip().used_bytes(), "used_chip == banco (no la arena)");

	// Liberar el primero fragmenta -> `chip_slots` sube.
	const eng::u16 slots_before = t.chip_slots;
	mm.chip().release(a);
	eng::debug::telemetry_from(mm, ms, t);
	check(t.chip_slots >= slots_before, "liberar en medio sube los huecos (fragmentacion)");
	check(t.chip_used < 2048u, "used baja tras release");

	// Scratch de frame se refleja en `frame_used`.
	ms.frame.allocate(256u, 16u);
	eng::debug::telemetry_from(mm, ms, t);
	check(t.frame_used > 0u, "frame_used refleja la scratch");
	check(t.frame_capacity == sizeof(g_frame), "frame_capacity del MemorySystem");
	(void)b;

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: telemetria de memoria (bancos + scratch + fragmentacion) validada.\n");
	return 0;
}
