// ============================================================================
// Test HOST-348: bancos de memoria del engine (eng::MemoryManager) - tipados.
// ============================================================================
//
// Respalda `eng/memory/memory_manager.hpp`: un bundle de bancos **tipados** (`MemBank<Chip/Slow/
// Fast>`). Cada reserva sale de un banco concreto (tag de plantilla), sin `MemoryKind` en runtime;
// un banco sin bytes (A500) devuelve bloques invalidos. Comprueba acceso por banco, free_bytes
// por banco y ausencia de bancos.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/348_memory_manager

#include <cstdio>

#include <eng/core/types/domains.hpp>
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

int main() {
	std::printf("== HOST-348 memory_manager ==\n");

	eng::u8 chip_buf[1024] {};
	eng::u8 slow_buf[512] {};
	eng::MemoryManager mm {};
	check(mm.configure(chip_buf, sizeof(chip_buf), slow_buf, sizeof(slow_buf), nullptr, 0u, 16u),
	      "configure");
	check(!mm.has_fast() && mm.has_slow(), "sin Fast, con Slow");

	// Reserva tipada por banco (sin switch, sin MemoryKind en runtime).
	const auto planes = mm.chip().reserve<eng::PlaneTag>(100u);
	check(planes.valid(), "Chip reserve");
	const auto tabla = mm.slow().reserve<eng::MapCellsTag>(64u);
	check(tabla.valid(), "Slow reserve");
	const auto sim = mm.fast().reserve<eng::PlaneTag>(16u);
	check(!sim.valid(), "Fast ausente -> invalido");

	check(mm.chip().free_bytes() < mm.chip().capacity(), "hueco Chip tras reservar");
	const eng::u32 slow_free = mm.slow().free_bytes();
	mm.slow().release(tabla);
	check(mm.slow().free_bytes() > slow_free, "release devuelve al banco");
	mm.chip().release(planes);
	check(mm.chip().free_bytes() == mm.chip().capacity(), "Chip restaurado");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: MemoryManager (bancos tipados + free_bytes por banco) validado.\n");
	return 0;
}
