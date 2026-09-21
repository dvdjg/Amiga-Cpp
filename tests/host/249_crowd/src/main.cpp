// ============================================================================
// Test HOST-249: crowd con vecinos por rejilla espacial (eng/ai/steering/crowd.hpp).
// ============================================================================
//
// Valida que `Crowd` actualiza los agentes (separacion, evasion, integracion) usando la rejilla
// espacial (`SpatialHash`) en vez de fuerza bruta `N^2`, y las reglas basicas: separacion efectiva,
// no interaccion a distancia, capas, agentes inactivos y limites de fuerza/velocidad.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/249_crowd

#include <cstdio>

#include <eng/ai/steering/crowd.hpp>

using namespace eng;
using namespace eng::ai;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

constexpr s16 kDt = 256; // 8.8: un tick

using TestCrowd = ai::Crowd<16, 32, 32, 128>;

CrowdAgent make_agent(s16 x, s16 y) {
	CrowdAgent a {};
	a.position = Point2s {x, y};
	return a;
}

s32 dist2(const CrowdAgent& a, const CrowdAgent& b) {
	const s32 dx = a.position.x - b.position.x;
	const s32 dy = a.position.y - b.position.y;
	return dx * dx + dy * dy;
}

/// Muchos agentes separados: la rejilla debe hacer MUCHAS menos comprobaciones que N^2.
void test_spatial_saving() {
	CrowdAgent agents[64] {};
	for (u16 i = 0; i < 64; ++i) {
		agents[i] = make_agent(static_cast<s16>(20 + (i % 8) * 40),
				       static_cast<s16>(20 + (i / 8) * 40));
	}
	TestCrowd crowd {};
	ai::CrowdParams p {};
	p.separation_radius = 18;
	const u32 checks = crowd.update(Span<CrowdAgent> {agents, 64}, p, kDt);
	const u32 brute = 64u * 63u; // pares ordenados de la fuerza bruta
	check(checks < brute / 4u, "la rejilla ahorra frente a N^2");
	check(checks >= 64u, "cada agente se comprueba a si mismo");
}

/// Dos agentes solapados: se separan con las actualizaciones.
void test_separation() {
	CrowdAgent agents[2] {make_agent(100, 100), make_agent(110, 100)};
	const s32 d0 = dist2(agents[0], agents[1]);
	TestCrowd crowd {};
	ai::CrowdParams p {};
	for (int i = 0; i < 8; ++i) {
		(void)crowd.update(Span<CrowdAgent> {agents, 2}, p, kDt);
	}
	const s32 d1 = dist2(agents[0], agents[1]);
	check(d1 > d0, "los agentes solapados se separan");
	check(agents[0].position.x < 100, "el agente 0 se mueve en contra del 1");
	check(agents[1].position.x > 110, "el agente 1 se mueve en contra del 0");
}

/// Dos agentes lejos y sin deseo: no interactuan.
void test_no_interaction() {
	CrowdAgent agents[2] {make_agent(100, 100), make_agent(300, 100)};
	TestCrowd crowd {};
	ai::CrowdParams p {};
	for (int i = 0; i < 4; ++i) {
		(void)crowd.update(Span<CrowdAgent> {agents, 2}, p, kDt);
	}
	check(agents[0].position.x == 100 && agents[0].position.y == 100, "agente lejano 0 quieto");
	check(agents[1].position.x == 300 && agents[1].position.y == 100, "agente lejano 1 quieto");
}

/// Capas distintas no se repelen.
void test_layers() {
	CrowdAgent agents[2] {make_agent(100, 100), make_agent(105, 100)};
	agents[0].layer = 0;
	agents[1].layer = 1;
	TestCrowd crowd {};
	ai::CrowdParams p {};
	for (int i = 0; i < 4; ++i) {
		(void)crowd.update(Span<CrowdAgent> {agents, 2}, p, kDt);
	}
	check(agents[0].position.x == 100 && agents[1].position.x == 105, "capas distintas no se repelen");
}

/// Los agentes inactivos no se mueven ni participan.
void test_inactive() {
	CrowdAgent agents[2] {make_agent(100, 100), make_agent(105, 100)};
	agents[1].flags = 0; // inactivo
	agents[1].desired = Point2s {1000, 0};
	TestCrowd crowd {};
	ai::CrowdParams p {};
	for (int i = 0; i < 4; ++i) {
		(void)crowd.update(Span<CrowdAgent> {agents, 2}, p, kDt);
	}
	check(agents[1].position.x == 105, "el inactivo no se mueve");
}

/// La velocidad se limita a `max_speed`.
void test_speed_clamp() {
	CrowdAgent agents[1] {make_agent(100, 100)};
	agents[0].desired = Point2s {1000, 0};
	agents[0].max_speed = 24;
	TestCrowd crowd {};
	ai::CrowdParams p {};
	for (int i = 0; i < 8; ++i) {
		(void)crowd.update(Span<CrowdAgent> {agents, 1}, p, kDt);
	}
	check(agents[0].velocity.x <= 24, "velocidad limitada a max_speed");
	check(agents[0].velocity.x == 24, "la velocidad llega a max_speed con deseo grande");
	check(agents[0].position.x > 100, "el agente avanza con deseo");
}

/// Los obstaculos estaticos empujan al agente.
void test_obstacles() {
	CrowdAgent agents[1] {make_agent(100, 100)};
	const Point2s obstacles[1] {Point2s {108, 100}};
	const s16 radii[1] {6};
	TestCrowd crowd {};
	ai::CrowdParams p {};
	for (int i = 0; i < 8; ++i) {
		(void)crowd.update(Span<CrowdAgent> {agents, 1}, Span<const Point2s> {obstacles, 1},
				   Span<const s16> {radii, 1}, p, kDt);
	}
	check(agents[0].position.x < 100, "el obstaculo empuja al agente en contra");
}

} // namespace

int main() {
	test_spatial_saving();
	test_separation();
	test_no_interaction();
	test_layers();
	test_inactive();
	test_speed_clamp();
	test_obstacles();

	if (failures == 0) {
		std::printf("OK: crowd (vecinos por rejilla, separacion, limites) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
