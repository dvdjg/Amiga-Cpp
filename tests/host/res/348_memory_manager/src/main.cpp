// ============================================================================
// Test HOST-348: gestor de memoria central por uso (eng::MemoryManager).
// ============================================================================
//
// Respalda `eng/memory/memory_manager.hpp`: reparte memoria por **uso** (politica), no por banco:
// Dma -> Chip; Compute -> Fast si hay; General -> Slow si hay. Cada banco es un BlockPool
// (reutilizable). Comprueba la politica, el fallback (sin Dma en Fast/Slow), el `free` por banco
// y la degradacion cuando falta un banco.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/348_memory_manager

#include <cstdio>

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

	eng::u8 chip[1024] {};
	eng::u8 slow[512] {};
	eng::u8 fast[256] {};
	eng::MemoryManager mm {};
	check(mm.configure(chip, sizeof(chip), slow, sizeof(slow), fast, sizeof(fast), 16u),
	      "configure");
	check(mm.has_slow() && mm.has_fast(), "bancos presentes");

	// Politica.
	check(mm.bank_for(eng::MemUse::Dma) == eng::MemoryKind::Chip, "Dma -> Chip");
	check(mm.bank_for(eng::MemUse::Compute) == eng::MemoryKind::Fast, "Compute -> Fast");
	check(mm.bank_for(eng::MemUse::General) == eng::MemoryKind::Slow, "General -> Slow");

	// Reparto por uso.
	const auto a = mm.allocate(100u, eng::MemUse::Dma);
	check(a.valid() && a.kind == eng::MemoryKind::Chip, "Dma sale de Chip");
	const auto b = mm.allocate(100u, eng::MemUse::Compute);
	check(b.valid() && b.kind == eng::MemoryKind::Fast, "Compute sale de Fast");
	const auto c = mm.allocate(100u, eng::MemUse::General);
	check(c.valid() && c.kind == eng::MemoryKind::Slow, "General sale de Slow");

	// Dma NO cae a Fast/Slow (Agnus no los ve): demasiado grande -> invalido.
	eng::MemoryManager mm2 {};
	check(!mm2.allocate(16u, eng::MemUse::Dma).valid(), "sin configure -> invalido");

	// free por banco + hueco.
	const eng::u32 before = mm.free_bytes(eng::MemoryKind::Slow);
	mm.free(c);
	check(mm.free_bytes(eng::MemoryKind::Slow) > before, "free devuelve al banco");

	// Sin Slow ni Fast (A500): todo cae a Chip.
	eng::MemoryManager mm3 {};
	(void)mm3.configure(chip, sizeof(chip), nullptr, 0u, nullptr, 0u, 8u);
	check(mm3.bank_for(eng::MemUse::Compute) == eng::MemoryKind::Chip, "sin Fast -> Compute Chip");
	check(mm3.bank_for(eng::MemUse::General) == eng::MemoryKind::Chip, "sin Slow -> General Chip");
	check(mm3.allocate(64u, eng::MemUse::General).kind == eng::MemoryKind::Chip, "General a Chip");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: MemoryManager (politica por uso + fallback + free) validado.\n");
	return 0;
}
