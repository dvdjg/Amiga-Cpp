// ============================================================================
// Test HOST-399: `ScrollPlan` común + `apply_scroll_plan` (§7(e))
// ============================================================================
//
// Verifica que el vocabulario declarativo común (`eng::playfield::ScrollPlan`) siembra la parte
// compartida de una config de motor (`apply_scroll_plan` sobre `XlimitedSceneConfigT`) sin pisar lo
// que no declara. El camino de tiras lo consume con `StripScrollLayer::set_plan` (ver 204).
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/field/399_scroll_plan

#include <cstdio>

#include <eng/field/scroll_plan.hpp>
#include <eng/field/xlimited_scroll_layer.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

using eng::playfield::apply_scroll_plan;
using eng::playfield::ScrollPlan;
using eng::playfield::TileLayerMap;
using eng::playfield::XlimitedSceneConfigT;

void test_seeds_geometry_and_palette() {
	eng::u16 pal[32] {};
	pal[0] = 0x123u;
	ScrollPlan plan {};
	plan.viewport_w = 384u;
	plan.viewport_h = 192u;
	plan.tile_w = 16u;
	plan.tile_h = 16u;
	plan.planes = 3u;
	plan.display_height = 288u;
	plan.map_period_words = 25u;
	plan.parallax_plane = 4u;
	plan.parallax_div = 2u;
	plan.tilemap.palette = eng::PaletteWords {pal, 32u};

	XlimitedSceneConfigT<TileLayerMap> cfg {};
	apply_scroll_plan(cfg, plan);

	check(cfg.viewport_w == 384u && cfg.viewport_h == 192u, "siembra viewport");
	check(cfg.tile_width == 16u && cfg.tile_height == 16u && cfg.planes == 3u, "siembra tiles/planos");
	check(cfg.display_height == 288u, "siembra display_height");
	check(cfg.palette.size() == 32u && cfg.palette.data()[0] == 0x123u, "siembra paleta");
	check(cfg.parallax_plane == 4u && cfg.parallax_div == 2u, "siembra parallax (RoboCod)");
}

void test_does_not_clobber_unspecified() {
	// display_height == 0 en el plan no pisa el default del motor.
	ScrollPlan p1 {};
	p1.viewport_w = 320u;
	XlimitedSceneConfigT<TileLayerMap> c1 {};
	c1.display_height = 999u;
	apply_scroll_plan(c1, p1);
	check(c1.display_height == 999u, "display_height=0 no pisa el existente");
	check(c1.viewport_w == 320u, "viewport sí se siembra");

	// paleta vacía en el plan no pisa la del juego.
	ScrollPlan p2 {};
	p2.viewport_w = 100u;
	XlimitedSceneConfigT<TileLayerMap> c2 {};
	apply_scroll_plan(c2, p2);
	check(c2.palette.size() == 0u, "paleta vacía no pisa");
}

} // namespace

int main() {
	test_seeds_geometry_and_palette();
	test_does_not_clobber_unspecified();
	if (failures == 0) {
		std::printf("OK: ScrollPlan común + apply_scroll_plan validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
