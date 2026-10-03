// ============================================================================
// Test HOST-407: estrategia Bands — layout de bandas (etapa 3 §7)
// ============================================================================
//
// Verifica `eng::scene::plan_bands`: valida las capas en banda (orden, sin solape, dentro del
// display) y produce los tramos. Puro, sin motores.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/scene/407_band_plan

#include <cstdio>

#include <eng/scene/band_plan.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

using eng::scene::BandPlanError;
using eng::scene::BandSpan;
using eng::scene::LayerPlacement;
using eng::scene::LayerRole;
using eng::scene::plan_bands;
using eng::scene::ScenePlan;

void test_split_screen() {
	ScenePlan<4u> plan {};
	(void)plan.add(LayerRole::Foreground, LayerPlacement {0u, 128u, 0u});
	(void)plan.add(LayerRole::Foreground, LayerPlacement {128u, 128u, 0u});

	BandSpan out[4] {};
	const auto r = plan_bands(plan.layers(), 256u, eng::Span<BandSpan> {out, 4u});
	check(r.has_value() && *r == 2u, "2 bandas válidas");
	if (r.has_value()) {
		check(out[0].top == 0u && out[0].height == 128u, "banda 0 arriba");
		check(out[1].top == 128u && out[1].height == 128u, "banda 1 abajo");
	}
}

void test_errors() {
	BandSpan out[4] {};

	ScenePlan<4u> overlap {};
	(void)overlap.add(LayerRole::Foreground, LayerPlacement {0u, 200u, 0u});
	(void)overlap.add(LayerRole::Foreground, LayerPlacement {100u, 100u, 0u});
	check(plan_bands(overlap.layers(), 256u, eng::Span<BandSpan> {out, 4u}).error() ==
		      BandPlanError::BadOrder,
	      "solape → BadOrder");

	ScenePlan<4u> out_of_range {};
	(void)out_of_range.add(LayerRole::Foreground, LayerPlacement {0u, 128u, 0u});
	(void)out_of_range.add(LayerRole::Foreground, LayerPlacement {128u, 200u, 0u});
	check(plan_bands(out_of_range.layers(), 256u, eng::Span<BandSpan> {out, 4u}).error() ==
		      BandPlanError::OutOfRange,
	      "se sale del display → OutOfRange");

	ScenePlan<4u> full {};
	(void)full.add(LayerRole::Background, LayerPlacement {});
	check(plan_bands(full.layers(), 256u, eng::Span<BandSpan> {out, 4u}).error() ==
		      BandPlanError::NotBands,
	      "capa a banda completa → NotBands");

	// Capacidad de salida insuficiente.
	ScenePlan<4u> two {};
	(void)two.add(LayerRole::Foreground, LayerPlacement {0u, 128u, 0u});
	(void)two.add(LayerRole::Foreground, LayerPlacement {128u, 128u, 0u});
	check(plan_bands(two.layers(), 256u, eng::Span<BandSpan> {out, 1u}).error() ==
		      BandPlanError::OutOfRange,
	      "salida corta → OutOfRange");
}

} // namespace

int main() {
	test_split_screen();
	test_errors();
	if (failures == 0) {
		std::printf("OK: layout de bandas (etapa 3 §7) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
