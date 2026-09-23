// ============================================================================
// Test HOST-139: presupuesto de memoria de eng::board (perfiles P20..P1M)
// ============================================================================
//
// Valida `engine/include/eng/board/core/budget.hpp`: footprint planificado por
// perfil, seleccion del mayor perfil que cabe en la RAM libre y monotonia de la
// tabla de transposicion.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/board/139_board_budget

#include <cstdio>

#include <eng/board/core/budget.hpp>

namespace {

using namespace eng::board;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_profile_footprints() {
	check(profile_plan(MemoryProfile::P20).planned_bytes() == 21504u, "perfil P20 ~21 kB");
	check(profile_plan(MemoryProfile::P64).planned_bytes() == 59392u, "perfil P64 ~58 kB");
	check(profile_plan(MemoryProfile::P128).planned_bytes() == 124928u, "perfil P128 ~122 kB");
	check(profile_plan(MemoryProfile::P256).planned_bytes() == 251904u, "perfil P256 ~246 kB");
	check(profile_plan(MemoryProfile::P512).planned_bytes() == 485376u, "perfil P512 ~474 kB");
	check(profile_plan(MemoryProfile::P1M).planned_bytes() == 976896u, "perfil P1M ~954 kB");
	check(profile_plan(MemoryProfile::P20).tt_entries == 0u, "P20 no usa TT");
}

void test_selection() {
	check(plan_memory(0u).profile == MemoryProfile::P20, "0 bytes -> fallback P20");
	check(plan_memory(20u * 1024u).profile == MemoryProfile::P20, "20 kB -> P20");
	check(plan_memory(64u * 1024u).profile == MemoryProfile::P64, "64 kB -> P64");
	check(plan_memory(128u * 1024u).profile == MemoryProfile::P128, "128 kB -> P128");
	check(plan_memory(256u * 1024u).profile == MemoryProfile::P256, "256 kB -> P256");
	check(plan_memory(512u * 1024u).profile == MemoryProfile::P512, "512 kB -> P512");
	check(plan_memory(1024u * 1024u).profile == MemoryProfile::P1M, "1 MB -> P1M");
}

void test_monotonic_tt() {
	const eng::u32 samples[] = {20u * 1024u, 64u * 1024u, 128u * 1024u, 256u * 1024u,
	                            512u * 1024u, 1024u * 1024u};
	eng::u32 previous = 0u;
	bool monotonic = true;
	for (eng::u32 free_bytes : samples) {
		const MemoryPlan plan = plan_memory(free_bytes);
		if (plan.tt_entries < previous) {
			monotonic = false;
		}
		previous = plan.tt_entries;
	}
	check(monotonic, "TT no decrece al crecer la RAM libre");
	check(plan_memory(1024u * 1024u).tt_entries > plan_memory(64u * 1024u).tt_entries,
	      "TT crece con la RAM libre");
	check(plan_memory(256u * 1024u).null_move && !plan_memory(20u * 1024u).null_move,
	      "null-move solo con memoria");
	check(plan_memory(1024u * 1024u).multi_pv == 4u, "P1M ofrece Multi-PV 4");
}

} // namespace

int main() {
	std::printf("eng::board budget:\n");
	test_profile_footprints();
	test_selection();
	test_monotonic_tt();

	if (g_fail == 0u) {
		std::printf("OK: eng::board budget (perfiles, seleccion, monotonia)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
