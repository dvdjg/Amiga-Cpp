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
#include <eng/memory/stack.hpp>

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

	// Banco Fast presente + `fast_or_slow` (CPU: Fast si hay, si no Slow).
	eng::u8 fast_buf[256] {};
	eng::MemoryManager mm2 {};
	check(mm2.configure(chip_buf, sizeof(chip_buf), slow_buf, sizeof(slow_buf), fast_buf,
			    sizeof(fast_buf), 16u),
	      "configure con Fast");
	check(mm2.has_fast(), "has_fast");
	const auto cpu_fast = eng::fast_or_slow<eng::PlaneTag>(mm2, 64u);
	check(cpu_fast.valid() && cpu_fast.kind == eng::MemoryKind::Fast, "fast_or_slow elige Fast");
	const auto cpu_slow = eng::fast_or_slow<eng::PlaneTag>(mm, 64u);
	check(cpu_slow.valid() && cpu_slow.kind == eng::MemoryKind::Slow, "fast_or_slow cae a Slow");

	// Politica nombrada (Fase 7): unifica la eleccion y deja el banco efectivo en `kind`.
	const auto p_chip = eng::reserve<eng::PlaneTag>(mm2, eng::MemoryPolicy::ChipRequired, 32u);
	check(p_chip.valid() && p_chip.kind == eng::MemoryKind::Chip, "policy ChipRequired -> Chip");
	const auto p_fast = eng::reserve<eng::PlaneTag>(mm2, eng::MemoryPolicy::FastRequired, 32u);
	check(p_fast.valid() && p_fast.kind == eng::MemoryKind::Fast, "policy FastRequired -> Fast");
	const auto p_fast_absent = eng::reserve<eng::PlaneTag>(mm, eng::MemoryPolicy::FastRequired, 32u);
	check(!p_fast_absent.valid(), "policy FastRequired sin Fast -> invalido (sin fallback)");
	const auto p_pref = eng::reserve<eng::PlaneTag>(mm, eng::MemoryPolicy::FastPreferred, 32u);
	check(p_pref.valid() && p_pref.kind == eng::MemoryKind::Slow, "policy FastPreferred cae a Slow");
	const auto p_pref_fast = eng::reserve<eng::PlaneTag>(mm2, eng::MemoryPolicy::FastPreferred, 32u);
	check(p_pref_fast.valid() && p_pref_fast.kind == eng::MemoryKind::Fast,
	      "policy FastPreferred elige Fast");
	const auto p_any = eng::reserve<eng::PlaneTag>(mm, eng::MemoryPolicy::AnyBank, 32u);
	check(p_any.valid() && p_any.kind == eng::MemoryKind::Slow, "policy AnyBank Fast->Slow");

	// Pila de CPU: banco seleccionable; tope alineado (valor para SP).
	const eng::Stack st = eng::fast_or_slow_stack(mm2, 128u);
	check(st.valid() && (st.top & 7u) == 0u && st.block.kind == eng::MemoryKind::Fast,
	      "fast_or_slow_stack (Fast, tope alineado)");
	const eng::Stack st_slow = eng::fast_or_slow_stack(mm, 128u);
	check(st_slow.valid() && st_slow.block.kind == eng::MemoryKind::Slow, "pila cae a Slow");
	const eng::Stack st_chip = eng::stack_from(mm.chip(), 128u);
	check(st_chip.valid() && st_chip.block.kind == eng::MemoryKind::Chip, "stack_from<Chip>");

	// StackPolicy (Fase 7, paso 3): banco/tamano/fallback en un valor. Banco Fast fresco (mm2 ya
	// esta lleno por las reservas anteriores).
	eng::u8 fast_buf2[512] {};
	eng::MemoryManager mm3 {};
	(void)mm3.configure(chip_buf, sizeof(chip_buf), slow_buf, sizeof(slow_buf), fast_buf2,
			    sizeof(fast_buf2), 16u);
	const eng::Stack sp_fast =
		eng::stack_from(mm3, eng::StackPolicy {eng::MemoryPolicy::FastPreferred, 128u, 8u});
	check(sp_fast.valid() && sp_fast.block.kind == eng::MemoryKind::Fast && (sp_fast.top & 7u) == 0u,
	      "StackPolicy FastPreferred -> Fast (tope alineado)");
	const eng::Stack sp_slow =
		eng::stack_from(mm, eng::StackPolicy {eng::MemoryPolicy::FastPreferred, 128u, 8u});
	check(sp_slow.valid() && sp_slow.block.kind == eng::MemoryKind::Slow, "StackPolicy cae a Slow");
	const eng::Stack sp_chip =
		eng::stack_from(mm, eng::StackPolicy {eng::MemoryPolicy::ChipRequired, 128u, 8u});
	check(sp_chip.valid() && sp_chip.block.kind == eng::MemoryKind::Chip, "StackPolicy ChipRequired");
	const eng::Stack sp_fast_req =
		eng::stack_from(mm, eng::StackPolicy {eng::MemoryPolicy::FastRequired, 128u, 8u});
	check(!sp_fast_req.valid(), "StackPolicy FastRequired sin Fast -> invalido (sin fallback)");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: MemoryManager (bancos tipados + free_bytes por banco) validado.\n");
	return 0;
}
