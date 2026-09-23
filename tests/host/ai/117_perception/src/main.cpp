// ============================================================================
// Test HOST-117: percepcion (mapa de influencia + memoria del agente)
// ============================================================================
//
// Valida:
//   - `ai/perception/influence_map.hpp`: `InfluenceMap<W,H>` (deposit/decay/at/
//     strongest).
//   - `ai/perception/agent_memory.hpp`: `AgentMemory` (see/tick/fresh/stale/forget).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ai/117_perception

#include <cstdio>

#include <eng/ai/perception/agent_memory.hpp>
#include <eng/ai/perception/influence_map.hpp>

namespace {

using eng::u16;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_influence() {
	eng::ai::InfluenceMap<4, 4> map;
	map.clear();
	check(map.strongest() == eng::ai::InfluenceMap<4, 4>::no_cell,
	      "influencia: mapa vacio -> no_cell");

	map.deposit(5u, 100);
	check(map.at(5u) == 100, "influencia: deposit acumula");
	check(map.strongest() == 5u, "influencia: la celda depositada manda");

	map.deposit(10u, 150);
	check(map.strongest() == 10u, "influencia: manda la mayor");

	map.decay(120);
	check(map.at(5u) == 0, "influencia: decay no baja de 0");
	check(map.at(10u) == 30, "influencia: decay resta");
	check(map.strongest() == 10u, "influencia: sigue mandando la mayor");

	map.decay(100);
	check(map.strongest() == eng::ai::InfluenceMap<4, 4>::no_cell,
	      "influencia: degradada del todo -> no_cell");
}

void test_memory() {
	eng::ai::AgentMemory mem;
	check(!mem.has_target, "memoria: arranca sin objetivo");
	check(!mem.fresh(0) && !mem.stale(0), "memoria: sin objetivo no es fresca ni caduca");

	mem.see({3, 4});
	check(mem.has_target && mem.last_position.x == 3 && mem.last_position.y == 4,
	      "memoria: guarda la posicion vista");
	check(mem.ticks_since_seen == 0u && mem.fresh(0), "memoria: recien vista es fresca");

	for (int i = 0; i < 5; ++i) {
		mem.tick();
	}
	check(mem.ticks_since_seen == 5u, "memoria: cuenta los ticks");
	check(mem.fresh(5) && !mem.stale(5), "memoria: fresca al limite");
	check(!mem.fresh(4) && mem.stale(4), "memoria: caducada por encima del limite");

	mem.forget();
	check(!mem.has_target && !mem.fresh(100), "memoria: olvidar");
}

} // namespace

int main() {
	std::printf("Perception:\n");
	test_influence();
	test_memory();

	if (g_fail == 0u) {
		std::printf("OK: Perception (influence map, agent memory)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
