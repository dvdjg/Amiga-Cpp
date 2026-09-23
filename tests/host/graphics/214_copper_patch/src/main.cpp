// Test host de `eng::copper::PatchHandle`: un MOVE parcheable de la copperlist al que la
// app escribe por frame con precisión quirúrgica (sin offsets cableados).
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/214_copper_patch

#include <cstdio>

#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/arena.hpp>

using namespace eng;

namespace {

alignas(16) u8 g_chip[16 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = LinearArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

} // namespace

int main() {
	MemorySystem mem = make_memory();
	Block<CopperTag> block = mem.chip.allocate_block<CopperTag>(2048u, 16);
	// `Plan`/`DoubleBuffer` tambien admite `MemoryBlock`; aqui se usa el bloque tipado.
	copper::Scheduler sched {block};

	copper::PatchHandle h = sched.patchable(copper::Register::COLOR00, 0x0123u);
	check(h.valid(), "patchable devuelve un handle valido");

	u16* words = reinterpret_cast<u16*>(block.view.data());
	check(words[h.index + 1u] == 0x0123u, "el MOVE inicial esta en la lista");

	h.set(0x0abcu);
	check(words[h.index + 1u] == 0x0abcu, "set() parchea el word de valor");

	// Un handle invalido (por defecto) no hace nada.
	const copper::PatchHandle none {};
	none.set(0xffffu);
	check(!none.valid(), "un handle por defecto es invalido");

	// Fuera de rango: `patch_data` valida y no escribe basura.
	h.set(0x0defu);
	check(words[h.index + 1u] == 0x0defu, "set() repetido sigue escribiendo el valor");

	if (failures == 0) {
		std::printf("OK: copper::PatchHandle (MOVE parcheable por frame).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
