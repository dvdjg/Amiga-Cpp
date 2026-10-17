// ============================================================================
// Test HOST-466: semantica de `frame_index` vs `field_index` (latido VBlank).
// ============================================================================
//
// Contrato del bucle por IRQ (`eng::VBlankHeartbeat` + `Engine::run_frames`,
// `engine.hpp`):
//   - `frame_index` cuenta **frames completados**; lo fija el bucle principal.
//   - `field_index` cuenta **latidos VBlank**; lo fija la IRQ (pacing del fondo).
//
// La IRQ NO debe pisar `frame_index`: hacerlo metia el contador de campos (50/s) en el
// de frames a mitad de frame, y una demo que lee `frame_index` en `render` (tras >1
// campo de trabajo) veia la tasa de campos — HUD de 079/116, 2026-10 (caso real: 49.2
// mostrado donde la tasa real era 32.7).
//
//   bash tools/run-host-tests.sh tests/host/os/466_heartbeat_frame_index

#include <cstdio>

#include <eng/engine.hpp>

namespace {

int g_fail = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_fail;                                                      \
		}                                                                 \
	} while (0)

struct DummyBackend {};
struct DummyGame {};

using HB = eng::VBlankHeartbeat<DummyBackend, DummyGame>;

int g_hooked = 0;
void hook(void* user) { ++*static_cast<int*>(user); }

/// La IRQ publica el latido en `field_index` y NO toca `frame_index`.
void test_irq_does_not_stomp_frame_index() {
	eng::GameContext ctx {};
	HB hb {&ctx, 0u, hook, &g_hooked};

	// Estado de un frame completado ya pintado por el bucle (p. ej. frame 250).
	ctx.frame.frame_index = 250u;

	HB::run(hb, 0u); // latido 0
	HB::run(hb, 0u); // latido 1
	HB::run(hb, 0u); // latido 2

	CHECK(ctx.frame.frame_index == 250u); // intacto: es del bucle, no de la IRQ
	CHECK(ctx.frame.field_index == 2u);   // ultimo latido publicado
	CHECK(hb.frames == 3u);               // contador de latidos del heartbeat
	CHECK(g_hooked == 3);                 // hook (mini-SO) una vez por latido
}

/// Sin hook, el latido sigue publicandose (el contador no depende del hook).
void test_field_index_advances() {
	eng::GameContext ctx {};
	HB hb {&ctx, 0u, nullptr, nullptr};
	HB::run(hb, 0u);
	CHECK(ctx.frame.field_index == 0u);
	CHECK(ctx.frame.frame_index == 0u);
	HB::run(hb, 0u);
	CHECK(ctx.frame.field_index == 1u);
	CHECK(hb.frames == 2u);
}

} // namespace

int main() {
	test_irq_does_not_stomp_frame_index();
	test_field_index_advances();
	if (g_fail == 0) {
		std::printf("HOST-466 heartbeat_frame_index: OK\n");
		return 0;
	}
	std::printf("HOST-466 heartbeat_frame_index: %d fallos\n", g_fail);
	return 1;
}
