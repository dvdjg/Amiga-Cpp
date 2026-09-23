// ============================================================================
// Test HOST-114: campo de flujo (Dijkstra multi-fuente + direcciones)
// ============================================================================
//
// Valida `engine/include/eng/ai/navigation/flow_field.hpp` sobre una rejilla 8x8:
//
//   1) Coste uniforme: el campo lleva del origen al objetivo por el camino minimo.
//   2) Terreno con coste: un muro obliga a rodearlo y el campo lo evita.
//   3) Region inalcanzable: integration = 0xffff y sin direccion.
//   4) Buffer vacio de objetivos -> false.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/114_flow_field

#include <cstdio>

#include <eng/ai/navigation/flow_field.hpp>

namespace {

using eng::u8;
using eng::u16;
using eng::usize;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

constexpr usize kN = 64;
u16 g_cost[kN] {};
u16 g_integration[kN] {};
u8 g_direction[kN] {};

void fill_cost(u16 value) {
	for (usize i = 0; i < kN; ++i) {
		g_cost[i] = value;
	}
}

bool compute(const u16* goals, usize count) {
	auto cost = [](u16 i) { return g_cost[i]; };
	return eng::ai::compute_flow_field<8, 8>(
		eng::Span<const u16> {goals, count}, cost, eng::Span<u16> {g_integration, kN},
		eng::Span<u8> {g_direction, kN});
}

/// Sigue el campo desde `start` hasta `goal`; devuelve los pasos, o `-1` si no llega.
int follow(u16 start, u16 goal) {
	u16 cur = start;
	for (int steps = 0; steps < 256; ++steps) {
		if (cur == goal) {
			return steps;
		}
		u16 next = 0;
		if (!eng::ai::flow_next<8>(eng::Span<const u8> {g_direction, kN}, cur, next)) {
			return -1;
		}
		cur = next;
	}
	return -1;
}

void test_uniform() {
	fill_cost(1u);
	const u16 goals[1] = {7u}; // (7,0)
	check(compute(goals, 1u), "flujo: calcula con coste uniforme");
	check(g_integration[7] == 0u, "flujo: el objetivo tiene coste 0");
	check(g_direction[7] == static_cast<u8>(eng::ai::FlowDir::None),
	      "flujo: el objetivo no tiene direccion");

	// De (0,7) [56] a (7,0) [7]: distancia Manhattan 7 + 7 = 14.
	check(g_integration[56] == 14u, "flujo: coste minimo Manhattan");
	check(follow(56u, 7u) == 14, "flujo: se llega en 14 pasos");
}

void test_terrain_detour() {
	fill_cost(1u);
	// Muro en x=4, y=0..5.
	for (u16 y = 0; y < 6u; ++y) {
		g_cost[static_cast<usize>(y) * 8u + 4u] = 0xffffu;
	}
	const u16 goals[1] = {7u}; // a la derecha del muro
	check(compute(goals, 1u), "flujo: calcula con muro");
	check(g_integration[0] != 0xffffu, "flujo: el origen alcanza el objetivo");
	const int steps = follow(0u, 7u);
	check(steps > 8, "flujo: la ruta rodea el muro (mas de 8 pasos)");
	// Comprobar que la ruta no pisa celdas bloqueadas.
	u16 cur = 0u;
	for (int i = 0; i < steps; ++i) {
		check(g_cost[cur] != 0xffffu, "flujo: la ruta no pisa celdas bloqueadas");
		u16 next = 0;
		if (!eng::ai::flow_next<8>(eng::Span<const u8> {g_direction, kN}, cur, next)) {
			break;
		}
		cur = next;
	}
}

void test_unreachable() {
	fill_cost(1u);
	// Fila y=4 completa bloqueada: separa arriba y abajo.
	for (u16 x = 0; x < 8u; ++x) {
		g_cost[static_cast<usize>(4u) * 8u + x] = 0xffffu;
	}
	const u16 goals[1] = {7u}; // arriba
	check(compute(goals, 1u), "flujo: calcula con fila bloqueada");
	check(g_integration[40] == 0xffffu, "flujo: region inalcanzable queda a 0xffff");
	check(follow(40u, 7u) == -1, "flujo: desde la region aislada no hay direccion");
}

void test_bad_input() {
	fill_cost(1u);
	check(!compute(nullptr, 0u), "flujo: sin objetivos devuelve false");
}

} // namespace

int main() {
	std::printf("FlowField:\n");
	test_uniform();
	test_terrain_detour();
	test_unreachable();
	test_bad_input();

	if (g_fail == 0u) {
		std::printf("OK: FlowField (camino minimo, muro, inalcanzable, sin objetivos)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
