// ============================================================================
// Test HOST-249: crowd generico con vecinos por rejilla espacial (eng/ai/steering/crowd.hpp).
// ============================================================================
//
// Valida que `Crowd<S, Broadphase>` actualiza los agentes (separacion, evasion, integracion)
// usando la rejilla espacial (`SpatialHash`) en vez de fuerza bruta `N^2`, y que el algoritmo es
// generico sobre el escalar (se prueba con `s32` y con `float`).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/249_crowd

#include <cstdio>

#include <eng/ai/steering/crowd.hpp>
#include <eng/core/fixed_math.hpp>
#include <eng/retro/fixed_q.hpp>

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

using V = eng::math::Vec<2, s32>;

CrowdParams<s32> params() {
	CrowdParams<s32> p {};
	p.separation_radius = 18;
	p.separation_weight = 40;
	p.obstacle_weight = 50;
	p.look_ahead = 12;
	p.max_force = 32;
	return p;
}

CrowdAgent<s32> make_agent(s32 x, s32 y) {
	CrowdAgent<s32> a {};
	a.position = V {{x, y}};
	a.radius = 6;
	a.max_speed = 24;
	return a;
}

s32 dist2(const CrowdAgent<s32>& a, const CrowdAgent<s32>& b) {
	const s32 dx = a.position.x() - b.position.x();
	const s32 dy = a.position.y() - b.position.y();
	return dx * dx + dy * dy;
}

using GridCrowd = Crowd<s32, SpatialHashBroadphase<16, 32, 32, 128>>;
using BruteCrowd = Crowd<s32, BruteForceBroadphase<s32, 128>>;

/// Muchos agentes separados: la rejilla debe hacer MUCHAS menos comprobaciones que N^2.
void test_spatial_saving() {
	CrowdAgent<s32> agents[64] {};
	for (u16 i = 0; i < 64; ++i) {
		agents[i] = make_agent(static_cast<s32>(20 + (i % 8) * 40),
				       static_cast<s32>(20 + (i / 8) * 40));
	}
	GridCrowd crowd {};
	const u32 checks = crowd.update(Span<CrowdAgent<s32>> {agents, 64}, params(), 1);
	const u32 brute = 64u * 63u;
	check(checks < brute / 4u, "la rejilla ahorra frente a N^2");
	check(checks >= 64u, "cada agente se comprueba a si mismo");
}

/// Dos agentes solapados: se separan con las actualizaciones (misma conducta con ambas fases).
void test_separation() {
	CrowdAgent<s32> agents[2] {make_agent(100, 100), make_agent(110, 100)};
	const s32 d0 = dist2(agents[0], agents[1]);
	GridCrowd crowd {};
	for (int i = 0; i < 8; ++i) {
		(void)crowd.update(Span<CrowdAgent<s32>> {agents, 2}, params(), 1);
	}
	check(dist2(agents[0], agents[1]) > d0, "los agentes solapados se separan");
	check(agents[0].position.x() < 100, "el agente 0 se mueve en contra del 1");
	check(agents[1].position.x() > 110, "el agente 1 se mueve en contra del 0");
}

/// La fuerza bruta da la misma separacion (mismo algoritmo, otra fase amplia).
void test_brute_same() {
	CrowdAgent<s32> a[2] {make_agent(100, 100), make_agent(110, 100)};
	CrowdAgent<s32> b[2] {make_agent(100, 100), make_agent(110, 100)};
	GridCrowd grid {};
	BruteCrowd brute {};
	for (int i = 0; i < 8; ++i) {
		(void)grid.update(Span<CrowdAgent<s32>> {a, 2}, params(), 1);
		(void)brute.update(Span<CrowdAgent<s32>> {b, 2}, params(), 1);
	}
	check(a[0].position.x() == b[0].position.x() && a[0].position.y() == b[0].position.y(),
	      "rejilla y fuerza bruta coinciden");
}

/// Dos agentes lejos y sin deseo: no interactuan.
void test_no_interaction() {
	CrowdAgent<s32> agents[2] {make_agent(100, 100), make_agent(300, 100)};
	GridCrowd crowd {};
	for (int i = 0; i < 4; ++i) {
		(void)crowd.update(Span<CrowdAgent<s32>> {agents, 2}, params(), 1);
	}
	check(agents[0].position.x() == 100 && agents[0].position.y() == 100, "agente lejano 0 quieto");
	check(agents[1].position.x() == 300 && agents[1].position.y() == 100, "agente lejano 1 quieto");
}

/// Capas distintas no se repelen.
void test_layers() {
	CrowdAgent<s32> agents[2] {make_agent(100, 100), make_agent(105, 100)};
	agents[1].layer = 1;
	GridCrowd crowd {};
	for (int i = 0; i < 4; ++i) {
		(void)crowd.update(Span<CrowdAgent<s32>> {agents, 2}, params(), 1);
	}
	check(agents[0].position.x() == 100 && agents[1].position.x() == 105, "capas distintas no repelen");
}

/// Los agentes inactivos no se mueven ni participan.
void test_inactive() {
	CrowdAgent<s32> agents[2] {make_agent(100, 100), make_agent(105, 100)};
	agents[1].flags = 0;
	agents[1].desired = V {{1000, 0}};
	GridCrowd crowd {};
	for (int i = 0; i < 4; ++i) {
		(void)crowd.update(Span<CrowdAgent<s32>> {agents, 2}, params(), 1);
	}
	check(agents[1].position.x() == 105, "el inactivo no se mueve");
}

/// La velocidad se limita a `max_speed`.
void test_speed_clamp() {
	CrowdAgent<s32> agents[1] {make_agent(100, 100)};
	agents[0].desired = V {{1000, 0}};
	GridCrowd crowd {};
	for (int i = 0; i < 8; ++i) {
		(void)crowd.update(Span<CrowdAgent<s32>> {agents, 1}, params(), 1);
	}
	check(agents[0].velocity.x() <= 24, "velocidad limitada a max_speed");
	check(agents[0].velocity.x() == 24, "la velocidad llega a max_speed con deseo grande");
	check(agents[0].position.x() > 100, "el agente avanza con deseo");
}

/// Los obstaculos estaticos empujan al agente.
void test_obstacles() {
	CrowdAgent<s32> agents[1] {make_agent(100, 100)};
	const V obstacles[1] {V {{108, 100}}};
	const s32 radii[1] {6};
	GridCrowd crowd {};
	for (int i = 0; i < 8; ++i) {
		(void)crowd.update(Span<CrowdAgent<s32>> {agents, 1}, Span<const V> {obstacles, 1},
				   Span<const s32> {radii, 1}, params(), 1);
	}
	check(agents[0].position.x() < 100, "el obstaculo empuja al agente en contra");
}

/// El mismo algoritmo con `float` (generico sobre el escalar).
void test_float_generic() {
	CrowdAgent<float> agents[2] {};
	agents[0].position = eng::math::Vec<2, float> {{100.0f, 100.0f}};
	agents[0].radius = 6.0f;
	agents[0].max_speed = 24.0f;
	agents[1] = agents[0];
	agents[1].position = eng::math::Vec<2, float> {{110.0f, 100.0f}};
	CrowdParams<float> p {};
	p.separation_radius = 18.0f;
	p.separation_weight = 40.0f;
	p.obstacle_weight = 50.0f;
	p.look_ahead = 12.0f;
	p.max_force = 32.0f;
	Crowd<float, BruteForceBroadphase<float, 8>> crowd {};
	for (int i = 0; i < 8; ++i) {
		(void)crowd.update(Span<CrowdAgent<float>> {agents, 2}, p, 1.0f);
	}
	check(agents[0].position.x() < 100.0f, "float: los agentes se separan");
	check(agents[1].position.x() > 110.0f, "float: el agente 1 se separa");
}

/// El mismo algoritmo con `q12` (fixed-point retro, 4.12).
void test_q12_generic() {
	using Q = eng::retro::q12;
	using VQ = eng::math::Vec<2, Q>;
	const Q one = eng::math::scalar_traits<Q>::from_int(1);
	const Q two = eng::math::scalar_traits<Q>::from_int(2);
	CrowdAgent<Q> agents[2] {};
	agents[0].position = VQ {{one, one}};
	agents[0].radius = eng::math::div_norm(one, two); // 0.5
	agents[0].max_speed = one;
	agents[1] = agents[0];
	agents[1].position = VQ {{two, one}};             // a 1.0 del otro
	CrowdParams<Q> p {};
	p.separation_radius = one;
	p.separation_weight = one;
	p.obstacle_weight = one;
	p.look_ahead = one;
	p.max_force = one;
	Crowd<Q, BruteForceBroadphase<Q, 8>> crowd {};
	for (int i = 0; i < 8; ++i) {
		(void)crowd.update(Span<CrowdAgent<Q>> {agents, 2}, p, one);
	}
	check(agents[0].position.x() < one, "q12: el agente 0 se separa");
	check(agents[1].position.x() > two, "q12: el agente 1 se separa");
}

} // namespace

int main() {
	test_spatial_saving();
	test_separation();
	test_brute_same();
	test_no_interaction();
	test_layers();
	test_inactive();
	test_speed_clamp();
	test_obstacles();
	test_float_generic();
	test_q12_generic();

	if (failures == 0) {
		std::printf("OK: crowd generico (rejilla, separacion, limites, float, q12) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
