// ============================================================================
// Test HOST-406: estrategia Dpf (etapa 2 §7)
// ============================================================================
//
// Verifica `eng::scene::apply_dpf_plan`: desde un `ScenePlan` con estrategia `Dpf` siembra la
// geometría/paleta comunes y marca los roles en una `XlimitedSceneConfigT`; rechaza planes que no
// son Dpf.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/scene/406_dpf_plan

#include <cstdio>

#include <eng/scene/dpf_plan.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

using eng::scene::apply_dpf_plan;
using eng::scene::LayerContent;
using eng::scene::LayerPlacement;
using eng::scene::LayerRole;
using eng::scene::ScenePlan;
using eng::scene::SceneStrategy;

void test_dpf_applied() {
	eng::u16 pal[16] {};
	pal[0] = 0x123u;
	eng::playfield::ScrollPlan bg_scroll {};
	bg_scroll.viewport_w = 384u;
	bg_scroll.viewport_h = 192u;
	bg_scroll.tile_w = 16u;
	bg_scroll.tile_h = 16u;
	bg_scroll.planes = 3u;
	bg_scroll.display_height = 288u;
	bg_scroll.tilemap.palette = eng::PaletteWords {pal, 16u};

	ScenePlan<4u> plan {};
	(void)plan.add(LayerRole::Background, LayerPlacement {}, bg_scroll);
	(void)plan.add(LayerRole::Foreground, LayerPlacement {0u, 0u, 2u}, bg_scroll); // FG en PF2
	check(plan.strategy() == SceneStrategy::Dpf, "el plan es Dpf");

	eng::playfield::XlimitedSceneConfigT<eng::playfield::TileLayerMap> cfg {};
	check(apply_dpf_plan(cfg, plan), "apply_dpf_plan OK");
	check(cfg.viewport_w == 384u && cfg.viewport_h == 192u, "geometría sembrada");
	check(cfg.planes == 3u && cfg.display_height == 288u, "planos + display_height");
	check(cfg.palette.size() == 16u && cfg.palette.data()[0] == 0x123u, "paleta sembrada");
	check(cfg.dpf.enabled && cfg.dpf.foreground_is_pf2 && !cfg.dpf.fg_canvas,
	      "DPF activado con FG delante en PF2");
}

void test_canvas_fg() {
	// FG como **lienzo estático** (rol `Foreground` + `content = Canvas`, p. ej. la 203).
	ScenePlan<4u> plan {};
	(void)plan.add(LayerRole::Background, LayerPlacement {});
	(void)plan.add(LayerRole::Foreground, LayerPlacement {0u, 0u, 2u},
		       eng::playfield::ScrollPlan {}, LayerContent::Canvas);
	check(plan.strategy() == SceneStrategy::Dpf, "canvas FG sigue siendo Dpf");

	eng::playfield::XlimitedSceneConfigT<eng::playfield::TileLayerMap> cfg {};
	check(apply_dpf_plan(cfg, plan), "apply con canvas FG");
	check(cfg.dpf.enabled && cfg.dpf.fg_canvas, "FG lienzo → fg_canvas=true");
}

void test_not_dpf() {
	ScenePlan<4u> single {};
	(void)single.add(LayerRole::Background, LayerPlacement {});
	eng::playfield::XlimitedSceneConfigT<eng::playfield::TileLayerMap> cfg {};
	check(!apply_dpf_plan(cfg, single), "plan Single → apply_dpf_plan false");
	check(!cfg.dpf.enabled, "no toca la config si no es Dpf");
}

} // namespace

int main() {
	test_dpf_applied();
	test_canvas_fg();
	test_not_dpf();
	if (failures == 0) {
		std::printf("OK: estrategia Dpf (etapa 2 §7) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
