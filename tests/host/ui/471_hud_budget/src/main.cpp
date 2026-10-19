// ============================================================================
// Test HOST-471: presupuesto y degradación del HUD (`eng/hud/budget.hpp`).
// ============================================================================
//
// Fase H4 del roadmap (`docs/guides/roadmap/ROADMAP_HUD.md`): coste de *backing*
// byte a byte contra referencia, encaje en el presupuesto declarado, conflicto
// de canales de sprite y **escalera de degradación determinista** bajo
// presupuesto simulado (sube con exceso, mantiene en la banda de histéresis,
// baja con holgura sostenida, tope en el nivel máximo y telemetría de cambios).
//
//   bash tools/run-host-tests.sh tests/host/ui/471_hud_budget

#include <cstdio>

#include <eng/hud/budget.hpp>

namespace {

int g_fail = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_fail;                                                      \
		}                                                                 \
	} while (0)

/// Coste de *backing* contra referencia calculada a mano: fila a palabra (16 px = 2 B),
/// por alto y por planos. Casos límite: ancho no múltiplo de 16, alto 1 y 6 planos (EHB).
void test_backing_bytes() {
	// 16x16 a 4 planos = 2 x 16 x 4 = 128 B; 96x24 a 4 planos = 12 x 24 x 4 = 1.152 B (el
	// panel B de la 305: 96 px/fila = 12 B, x 24 filas x 4 planos).
	CHECK(eng::hud::hud_backing_bytes(16u, 16u, 4u) == 128u);
	CHECK(eng::hud::hud_backing_bytes(96u, 24u, 4u) == 1152u);
	// Ancho 17 -> 2 palabras (4 B) por fila: 4 x 8 x 2 = 64 B.
	CHECK(eng::hud::hud_backing_bytes(17u, 8u, 2u) == 64u);
	// Ancho 1 -> 1 palabra (2 B): 2 x 1 x 1 = 2 B; 6 planos (EHB): 2 x 4 x 6 = 48 B.
	CHECK(eng::hud::hud_backing_bytes(1u, 1u, 1u) == 2u);
	CHECK(eng::hud::hud_backing_bytes(16u, 4u, 6u) == 48u);
	// Cero de cualquier dimensión = coste 0 (caja vacía no reserva).
	CHECK(eng::hud::hud_backing_bytes(0u, 16u, 4u) == 0u);
	CHECK(eng::hud::hud_backing_bytes(16u, 0u, 4u) == 0u);

	eng::hud::HudBudget budget {};
	budget.max_backing_bytes = 1152u;
	CHECK(eng::hud::hud_backing_fits(budget, 96u, 24u, 4u));      // justo en el límite
	CHECK(!eng::hud::hud_backing_fits(budget, 96u, 25u, 4u));     // una fila de más no cabe
	CHECK(!eng::hud::hud_backing_fits(budget, 97u, 24u, 4u));     // una palabra de más no cabe
	budget.max_backing_bytes = 0u;                                // sin presupuesto: no cabe nada
	CHECK(!eng::hud::hud_backing_fits(budget, 16u, 16u, 1u));
}

/// Solape de reservas de canales de sprite: dos máscaras con un bit común es *setup* inválido.
void test_channel_conflict() {
	CHECK(!eng::hud::hud_channels_conflict(0x03u, 0x0cu)); // canales 0-1 y 2-3: disjuntos
	CHECK(eng::hud::hud_channels_conflict(0x03u, 0x02u));  // comparten el canal 1
	CHECK(eng::hud::hud_channels_conflict(0x80u, 0x80u));  // el mismo canal
	CHECK(!eng::hud::hud_channels_conflict(0x00u, 0xffu)); // sin reserva previa: sin conflicto
}

/// Escalera determinista bajo presupuesto simulado: 8.000 ciclos de presupuesto, umbral de
/// salida 7.000 (7/8). La misma secuencia de medidas da la misma de niveles y cambios.
void test_degrade_ladder() {
	eng::hud::HudDegradeLadder lad {};
	CHECK(lad.level() == 0u);
	CHECK(!lad.degraded());
	CHECK(!lad.update(100000u)); // sin configurar: nunca degrada

	lad.configure(8000u);
	CHECK(lad.budget_cycles() == 8000u);
	CHECK(!lad.update(7000u));  // 7.000 no supera el presupuesto: nivel 0
	CHECK(lad.level() == 0u);
	CHECK(!lad.update(7001u)); // dentro de la banda (7.000..8.000): mantiene el nivel 0
	CHECK(lad.update(8001u));  // 8.001 supera el presupuesto: sube a 1
	CHECK(lad.level() == 1u);
	CHECK(lad.degraded());
	CHECK(lad.update(8001u));   // sigue por encima: sube a 2
	CHECK(lad.level() == 2u);
	CHECK(!lad.update(7500u));  // en la banda de histéresis (>= 7.000): mantiene
	CHECK(lad.level() == 2u);
	CHECK(lad.update(6999u));   // por debajo del umbral: baja a 1
	CHECK(lad.level() == 1u);
	CHECK(lad.update(1000u));   // holgura: baja a 0
	CHECK(lad.level() == 0u);
	CHECK(!lad.degraded());
	CHECK(lad.changes() == 4u); // subida x2, bajada x2

	// Tope: el nivel no pasa del máximo aplicable (2 aquí) aunque se exceda siempre.
	eng::hud::HudDegradeLadder capped {};
	capped.configure(1000u, 2u);
	CHECK(capped.update(2000u));
	CHECK(capped.update(2000u));
	CHECK(!capped.update(2000u)); // ya en el tope
	CHECK(capped.level() == 2u);
	CHECK(capped.changes() == 2u);
	// Con max_level mayor que la escalera canónica se recorta a 6.
	eng::hud::HudDegradeLadder big {};
	big.configure(1000u, 200u);
	for (int i = 0; i < 20; ++i) {
		(void)big.update(2000u);
	}
	CHECK(big.level() == eng::hud::kHudDegradeMaxLevel);

	// Determinismo: repetir la misma secuencia reproduce niveles y número de cambios.
	const eng::u32 seq[8] = {9000u, 9000u, 7500u, 9000u, 1000u, 1000u, 9000u, 6500u};
	eng::hud::HudDegradeLadder a {};
	eng::hud::HudDegradeLadder b {};
	a.configure(8000u);
	b.configure(8000u);
	for (int i = 0; i < 8; ++i) {
		(void)a.update(seq[i]);
		(void)b.update(seq[i]);
	}
	CHECK(a.level() == b.level());
	CHECK(a.changes() == b.changes());
}

/// **Telemetría por región** (H4): acumulación (`add`), reinicio por frame (`reset`) y
/// estado degradado agregado; el índice fuera de rango cae en la región 0 (documentado).
void test_region_stats() {
	eng::hud::HudRegionStats a {};
	a.ops = 10u;
	a.blits = 2u;
	a.copper_moves = 3u;
	a.sprite_channels = 0x03u;
	a.bobs = 4u;
	a.backing_bytes = 100u;
	a.degrade_level = 1u;
	eng::hud::HudRegionStats b {};
	b.ops = 5u;
	b.sprite_channels = 0x0cu;
	b.bobs = 1u;
	b.backing_bytes = 50u;
	b.degrade_level = 3u;
	a.add(b);
	CHECK(a.ops == 15u);
	CHECK(a.blits == 2u);
	CHECK(a.copper_moves == 3u);
	CHECK(a.sprite_channels == 0x0fu); // máscaras: OR
	CHECK(a.bobs == 5u);
	CHECK(a.backing_bytes == 150u);
	CHECK(a.degrade_level == 3u); // el nivel más alto manda

	eng::hud::HudStats stats {};
	CHECK(stats.count == 0u);
	CHECK(!stats.degraded());
	stats.count = 2u;
	stats.region(0u).ops = 7u;
	stats.region(1u).degrade_level = 2u;
	CHECK(stats.degraded());
	CHECK(stats.region(1u).degrade_level == 2u);
	CHECK(stats.region(200u).ops == 7u); // fuera de rango -> región 0 (fallback documentado)
	stats.changes = 9u;
	stats.reset();
	CHECK(stats.region(0u).ops == 0u);
	CHECK(stats.region(1u).degrade_level == 0u);
	CHECK(!stats.degraded());
	CHECK(stats.changes == 9u); // el reset del frame no borra la telemetría acumulada
}

/// **Comprobador de presupuesto** (H4): el primer campo excedido en el orden de la escalera,
/// con presupuesto 0 = sin límite (salvo la máscara de sprites, que en 0 no reserva nada).
void test_budget_check() {
	eng::hud::HudBudget budget {};
	eng::hud::HudRegionStats used {};
	// Sin límites declarados y sin gasto: dentro.
	CHECK(eng::hud::hud_budget_check(budget, used) == eng::hud::HudBudgetOver::None);
	budget.max_draw_ops = 20u;
	budget.max_blits_per_frame = 4u;
	budget.max_backing_bytes = 512u;
	budget.max_bobs = 2u;
	budget.sprite_channel_mask = 0x03u; // canales 0 y 1 reservados
	used.ops = 20u;
	used.blits = 4u;
	used.backing_bytes = 512u;
	used.bobs = 2u;
	used.sprite_channels = 0x03u;
	CHECK(eng::hud::hud_budget_check(budget, used) == eng::hud::HudBudgetOver::None); // justo
	used.ops = 21u;
	CHECK(eng::hud::hud_budget_check(budget, used) == eng::hud::HudBudgetOver::DrawOps);
	used.ops = 20u;
	used.blits = 5u;
	CHECK(eng::hud::hud_budget_check(budget, used) == eng::hud::HudBudgetOver::Blits);
	used.blits = 4u;
	used.backing_bytes = 513u;
	CHECK(eng::hud::hud_budget_check(budget, used) == eng::hud::HudBudgetOver::BackingBytes);
	used.backing_bytes = 512u;
	used.sprite_channels = 0x05u; // canal 2 fuera de la máscara reservada (0x03)
	CHECK(eng::hud::hud_budget_check(budget, used) == eng::hud::HudBudgetOver::SpriteChannels);
	used.sprite_channels = 0x03u;
	used.bobs = 3u;
	CHECK(eng::hud::hud_budget_check(budget, used) == eng::hud::HudBudgetOver::Bobs);
	used.bobs = 2u;
	// Con máscara 0 (sin canales reservados) cualquier uso de sprite es exceso.
	eng::hud::HudBudget no_spr {};
	used.sprite_channels = 0x01u;
	CHECK(eng::hud::hud_budget_check(no_spr, used) == eng::hud::HudBudgetOver::SpriteChannels);
	used.sprite_channels = 0u;
	CHECK(eng::hud::hud_budget_check(no_spr, used) == eng::hud::HudBudgetOver::None);
}

} // namespace

int main() {
	test_backing_bytes();
	test_channel_conflict();
	test_degrade_ladder();
	test_region_stats();
	test_budget_check();
	if (g_fail == 0) {
		std::printf("HOST-471 hud_budget: OK\n");
		return 0;
	}
	std::printf("HOST-471 hud_budget: %d fallos\n", g_fail);
	return 1;
}
