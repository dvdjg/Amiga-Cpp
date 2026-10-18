// ============================================================================
// Test HOST-471: pacing de render (`eng::graphics/render_pacing.hpp`)
// ============================================================================
//
// Valida la regla que convierte el coste medido de la pasada completa en el numero
// de frames intermedios (Fake) por ciclo de presentacion (fase F1 del
// ROADMAP_3D_PRESENTACION: base >= 12,5 Hz y presentacion >= 25 Hz):
//
//   1) Sin medida: 0 fakes (Full por tick; el llamador seguira el camino conservador).
//   2) Campos enteros (techo) del coste: 1/2/3/4 campos -> ceil correcto.
//   3) Regla K = max(0, campos - 2) y clamps: cap, y el caso F <= 2 sin fakes.
//   4) Media movil 1/4: un pico aislado no dispara la racha (y converge).
//   5) Caso objetivo de F1: 3 campos -> K=1 -> ciclo de 4 campos (12,5/25 Hz).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/471_render_pacing

#include <cstdio>

#include <eng/graphics/render_pacing.hpp>

namespace {

using eng::graphics::RenderPacing;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_no_measure() {
	RenderPacing p {};
	check(!p.valid(), "sin medida no es valido");
	check(p.affordable_fakes(4u) == 0u, "sin medida: 0 fakes (Full por tick)");
}

void test_fields_round() {
	RenderPacing p {};
	const eng::u32 tick = p.tick_cycles;
	p.observe(tick); // exactamente 1 campo
	check(p.full_fields() == 1u, "1 campo exacto");
	p.observe(tick); // la EMA del mismo valor no cambia
	check(p.full_fields() == 1u, "1 campo estable tras varias muestras");
	p = {};
	p.observe(tick / 2u); // medio campo -> redondeo 1
	check(p.full_fields() == 1u, "medio campo -> 1");
	p = {};
	p.observe(tick + 1u); // un ciclo por encima de 1 campo -> redondeo 1
	check(p.full_fields() == 1u, "1 campo + 1 ciclo -> 1 (redondeo)");
	p = {};
	p.observe(tick * 2u);
	check(p.full_fields() == 2u, "2 campos exactos");
	p = {};
	p.observe(tick * 3u / 2u + tick / 8u); // 1,625 campos -> redondeo 2
	check(p.full_fields() == 2u, "1,625 campos -> 2 (redondeo)");
}

void test_rule_and_cap() {
	RenderPacing p {};
	const eng::u32 tick = p.tick_cycles;
	p.observe(tick);
	check(p.affordable_fakes(4u) == 0u, "F=1: 0 fakes (Full a 50 Hz ya cumple)");
	p = {};
	p.observe(2u * tick);
	check(p.affordable_fakes(4u) == 0u, "F=2: 0 fakes (Full a 25 Hz ya cumple)");
	p = {};
	p.observe(3u * tick);
	check(p.affordable_fakes(4u) == 1u, "F=3: 1 fake -> ciclo 4 campos (12,5/25 Hz)");
	p = {};
	p.observe(4u * tick);
	check(p.affordable_fakes(4u) == 2u, "F=4: 2 fakes (prioriza presentacion >=25 Hz)");
	p = {};
	p.observe(6u * tick);
	check(p.affordable_fakes(4u) == 4u, "F=6: 4 fakes");
	p = {};
	p.observe(6u * tick);
	check(p.affordable_fakes(2u) == 2u, "cap=2 recorta la racha");
	p = {};
	p.observe(6u * tick);
	check(p.affordable_fakes(0u) == 0u, "cap=0 fuerza Full por tick");
}

void test_ema() {
	RenderPacing p {};
	const eng::u32 tick = p.tick_cycles;
	// Primer Full barato (1 campo); un pico de 4 campos entra al 25% (EMA 1/4):
	// 1 + (4-1)/4 = 1.75 campos -> redondeo 2 -> aun 0 fakes.
	p.observe(tick);
	p.observe(4u * tick);
	check(p.full_fields() == 2u, "pico aislado suavizado por la EMA (2 campos)");
	// Varios Fulls caros seguidos convergen a 4 campos (3/4 del valor previo + 1/4).
	for (int i = 0; i < 8; ++i) {
		p.observe(4u * tick);
	}
	check(p.full_fields() == 4u, "racha de Fulls caros converge a 4 campos");
	check(p.affordable_fakes(4u) == 2u, "y la regla da 2 fakes");
	// Volver a Fulls baratos: la EMA baja y la racha se recorta sola.
	for (int i = 0; i < 12; ++i) {
		p.observe(tick);
	}
	check(p.full_fields() == 1u, "converge de vuelta a 1 campo");
	check(p.affordable_fakes(4u) == 0u, "sin fakes cuando el Full vuelve a ser barato");
}

void test_f1_target() {
	// Caso objetivo de la 080: Full de ~3 campos de media.
	RenderPacing p {};
	const eng::u32 tick = p.tick_cycles;
	for (int i = 0; i < 5; ++i) {
		p.observe(3u * tick + tick / 8u); // 3,125 campos medios
	}
	check(p.full_fields() == 3u, "3,125 campos -> 3 (redondeo)");
	check(p.affordable_fakes(2u) == 1u, "3,125 campos: 1 fake (cap 2)");
	// Con ~3,6 campos medios el redondeo da 4 -> 2 fakes.
	p = {};
	for (int i = 0; i < 5; ++i) {
		p.observe(18u * tick / 5u); // 3,6 campos medios
	}
	check(p.full_fields() == 4u, "3,6 campos -> 4");
	check(p.affordable_fakes(3u) == 2u, "3,6 campos: 2 fakes");
	// Con 2,6 campos -> 3 -> 1 fake -> ciclo 4 campos (12,5/25 Hz).
	p = {};
	for (int i = 0; i < 5; ++i) {
		p.observe(13u * tick / 5u);
	}
	check(p.full_fields() == 3u, "2,6 campos -> 3");
	check(p.affordable_fakes(3u) == 1u, "2,6 campos: 1 fake -> ciclo 4 campos (12,5/25 Hz)");
}

} // namespace

int main() {
	std::printf("Pacing de render (render_pacing):\n");
	test_no_measure();
	test_fields_round();
	test_rule_and_cap();
	test_ema();
	test_f1_target();
	if (g_fail != 0u) {
		std::printf("FALLOS: %u\n", g_fail);
		return 1;
	}
	std::printf("OK: render pacing (campos, regla de fakes, cap y media movil)\n");
	return 0;
}
