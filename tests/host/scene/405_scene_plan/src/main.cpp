// ============================================================================
// Test HOST-405: vocabulario de escena + elección de estrategia (etapa 1 §7)
// ============================================================================
//
// Verifica `eng::scene::ScenePlan`/`choose_strategy`: a partir de las capas declaradas (rol +
// colocación) elige Single/Dpf/Bands o rechaza (Unsupported). Puro, sin motores.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/scene/405_scene_plan

#include <cstdio>

#include <eng/scene/plan.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

using eng::scene::choose_strategy;
using eng::scene::LayerPlacement;
using eng::scene::LayerRole;
using eng::scene::ScenePlan;
using eng::scene::SceneStrategy;

constexpr LayerPlacement kFull {};
constexpr LayerPlacement kBandTop {0u, 128u, 0u};
constexpr LayerPlacement kBandBottom {128u, 128u, 0u};

void test_empty_and_single() {
	ScenePlan<4u> empty {};
	check(empty.strategy() == SceneStrategy::Empty, "sin capas → Empty");

	ScenePlan<4u> single {};
	(void)single.add(LayerRole::Background, kFull);
	check(single.strategy() == SceneStrategy::Single, "1 capa full → Single");
}

void test_dpf() {
	ScenePlan<4u> dpf {};
	(void)dpf.add(LayerRole::Background, kFull);
	(void)dpf.add(LayerRole::Foreground, kFull);
	check(dpf.strategy() == SceneStrategy::Dpf, "BG+FG full → Dpf");

	ScenePlan<4u> same_roles {};
	(void)same_roles.add(LayerRole::Background, kFull);
	(void)same_roles.add(LayerRole::Background, kFull);
	check(same_roles.strategy() == SceneStrategy::Unsupported, "BG+BG full → Unsupported");

	ScenePlan<4u> overlay {};
	(void)overlay.add(LayerRole::Background, kFull);
	(void)overlay.add(LayerRole::Overlay, kFull);
	check(overlay.strategy() == SceneStrategy::Unsupported, "BG+Overlay full → Unsupported");
}

void test_bands() {
	ScenePlan<4u> bands {};
	(void)bands.add(LayerRole::Foreground, kBandTop);
	(void)bands.add(LayerRole::Foreground, kBandBottom);
	check(bands.strategy() == SceneStrategy::Bands, "2 bandas → Bands");

	ScenePlan<4u> one_band {};
	(void)one_band.add(LayerRole::Foreground, kBandTop);
	check(one_band.strategy() == SceneStrategy::Unsupported, "1 banda → Unsupported");

	ScenePlan<4u> mixed {};
	(void)mixed.add(LayerRole::Background, kFull);
	(void)mixed.add(LayerRole::Foreground, kBandTop);
	check(mixed.strategy() == SceneStrategy::Unsupported, "full + banda → Unsupported");
}

void test_placement_validation() {
	LayerPlacement bad {200u, 100u, 0u};
	check(bad.ok(), "top+height > top → ok");
	LayerPlacement zero_h {10u, 0u, 0u};
	check(zero_h.full(), "height 0 → full");
}

} // namespace

int main() {
	test_empty_and_single();
	test_dpf();
	test_bands();
	test_placement_validation();
	if (failures == 0) {
		std::printf("OK: vocabulario de escena + estrategia (etapa 1 §7) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
