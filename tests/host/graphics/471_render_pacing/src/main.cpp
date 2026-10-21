// ============================================================================
// Test HOST-471: pacing de render (`eng::graphics/render_pacing.hpp`)
// ============================================================================
//
// Valida la regla que convierte el coste medido de la pasada completa en el numero
// de frames intermedios (Fake) por ciclo de presentacion (fase F1 del
// ROADMAP_3D_PRESENTACION: base >= 12,5 Hz y presentacion >= 25 Hz):
//
//   1) Sin medida: 0 fakes (Full por tick; el llamador seguira el camino conservador).
//   2) Campos enteros: cualquier fracción sobre el tick cuenta como el campo siguiente.
//   3) Regla K = max(0, campos - 2), cap de 3 y casos F <= 2 sin fakes.
//   4) El máximo del último par reacciona a un Full caro y no baja por uno barato aislado.
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
	p.observe(tick + 1u); // cualquier resto ocupa el siguiente campo de presentación
	check(p.full_fields() == 2u, "1 campo + 1 ciclo -> 2 (techo conservador)");
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

void test_latest_pair_max() {
	RenderPacing p {};
	const eng::u32 tick = p.tick_cycles;
	p.observe(3u * tick);
	check(p.full_fields() == 3u && p.affordable_fakes(3u) == 1u,
	      "Full de 3 campos selecciona 1 Fake");
	p.observe(4u * tick);
	check(p.full_fields() == 4u && p.affordable_fakes(3u) == 2u,
	      "Full reciente de 4 campos sube inmediatamente a 2 Fakes");
	p.observe(2u * tick);
	check(p.full_fields() == 4u && p.affordable_fakes(3u) == 2u,
	      "un Full barato no reduce la decisión guardada del anterior caro");
	p.observe(2u * tick);
	check(p.full_fields() == 2u && p.affordable_fakes(3u) == 0u,
	      "dos Fulls baratos consecutivos liberan la racha anterior");
	p.observe(6u * tick);
	check(p.full_fields() == 6u && p.affordable_fakes(3u) == 3u,
	      "el tope de 3 Fakes limita los Fulls más lentos");
}

void test_f1_target() {
	// Los costes se clasifican por el techo en campos del máximo de las dos últimas pasadas.
	RenderPacing p {};
	const eng::u32 tick = p.tick_cycles;
	p.observe(3u * tick + 1u); // un ciclo por encima de 3 campos ya ocupa el cuarto
	check(p.full_fields() == 4u, "3 campos + 1 ciclo -> techo 4 campos");
	check(p.affordable_fakes(3u) == 2u, "4 campos seleccionan 2 Fakes");
	p.observe(2u * tick);
	check(p.full_fields() == 4u, "la muestra anterior mantiene la cota conservadora");
	p.observe(2u * tick);
	check(p.full_fields() == 2u && p.affordable_fakes(3u) == 0u,
	      "dos muestras de 2 campos vuelven al modo sin interpolados");
}

} // namespace

int main() {
	std::printf("Pacing de render (render_pacing):\n");
	test_no_measure();
	test_fields_round();
	test_rule_and_cap();
	test_latest_pair_max();
	test_f1_target();
	if (g_fail != 0u) {
		std::printf("FALLOS: %u\n", g_fail);
		return 1;
	}
	std::printf("OK: render pacing (máximo del último par, techo de campos y cap 0..3)\n");
	return 0;
}
