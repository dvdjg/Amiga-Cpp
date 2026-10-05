// ============================================================================
// Test HOST-071: seleccion nombrada de variantes de scroll (eng/field/scroll_variant.hpp).
// ============================================================================
//
// Verifica que los nombres de la referencia ScrollingTricks se traducen a los
// ejes/wrap/fetch de `XlimitedConfigT`, y los presets tall-Y / wide-X.
//
//   bash tools/run-host-tests.sh tests/host/field/071_scroll_variant

#include <cstdio>

#include <eng/field/scroll_variant.hpp>
#include <eng/scene/scroll_plan.hpp>

using namespace eng;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

playfield::XlimitedConfig base_cfg() {
	playfield::XlimitedConfig cfg {};
	cfg.viewport_w = 320u;
	cfg.viewport_h = 256u;
	cfg.tile_width = 16u;
	cfg.tile_height = 16u;
	cfg.map.width = 64u;
	cfg.map.height = 18u;
	return cfg;
}

} // namespace

int main() {
	std::printf("== HOST-071 scroll_variant ==\n");

	// XLimited: X anillo, Y off, sin wrap, fetch normal.
	{
		auto cfg = base_cfg();
		playfield::apply_scroll_variant(cfg, playfield::ScrollVariant::XLimited);
		check(cfg.x_mode == playfield::AxisPolicy::Ring, "XLimited: X ring");
		check(cfg.y_mode == playfield::AxisPolicy::Off, "XLimited: Y off");
		check(cfg.map.wrap_x == 0u && cfg.map.wrap_y == 0u, "XLimited: sin wrap");
		check(cfg.fetch_mode == 0u && cfg.bitmap_width == 0u, "XLimited: fetch normal (auto)");
	}

	// XUnlimited: X toroidal.
	{
		auto cfg = base_cfg();
		playfield::apply_scroll_variant(cfg, playfield::ScrollVariant::XUnlimited);
		check(cfg.x_mode == playfield::AxisPolicy::Ring && cfg.y_mode == playfield::AxisPolicy::Off,
		      "XUnlimited: ejes");
		check(cfg.map.wrap_x == 64u && cfg.map.wrap_y == 0u, "XUnlimited: wrap X = width");
	}

	// YUnlimited: Y anillo, toroidal.
	{
		auto cfg = base_cfg();
		playfield::apply_scroll_variant(cfg, playfield::ScrollVariant::YUnlimited);
		check(cfg.x_mode == playfield::AxisPolicy::Off && cfg.y_mode == playfield::AxisPolicy::Ring,
		      "YUnlimited: ejes");
		check(cfg.map.wrap_x == 0u && cfg.map.wrap_y == 18u, "YUnlimited: wrap Y = height");
	}

	// XYLimited: ambos anillo, sin wrap.
	{
		auto cfg = base_cfg();
		playfield::apply_scroll_variant(cfg, playfield::ScrollVariant::XYLimited);
		check(cfg.x_mode == playfield::AxisPolicy::Ring && cfg.y_mode == playfield::AxisPolicy::Ring,
		      "XYLimited: ambos ring");
		check(cfg.map.wrap_x == 0u && cfg.map.wrap_y == 0u, "XYLimited: sin wrap");
	}

	// XYUnlimited: ambos anillo, wrap ambos.
	{
		auto cfg = base_cfg();
		playfield::apply_scroll_variant(cfg, playfield::ScrollVariant::XYUnlimited);
		check(cfg.map.wrap_x == 64u && cfg.map.wrap_y == 18u, "XYUnlimited: wrap ambos");
	}

	// Variante _64: fetch 1x4x y bitmap = viewport + 64.
	{
		auto cfg = base_cfg();
		playfield::apply_scroll_variant(cfg, playfield::ScrollVariant::XYLimited, /*wide_fetch=*/true);
		check(cfg.fetch_mode == 3u, "_64: fetch 1x4x");
		check(cfg.bitmap_width == 384u, "_64: bitmap = 320 + 64");
	}

	// XLimited con Y > viewport.
	check(playfield::xlimited_tall_y_display(208u, 16u) == 240u, "tall-Y: display = 208 + 2*16");

	// YLimited con X ancha.
	{
		auto cfg = base_cfg();
		playfield::apply_ylimited_wide_x(cfg);
		check(cfg.y_mode == playfield::AxisPolicy::Ring && cfg.x_mode == playfield::AxisPolicy::Off,
		      "wide-X: Y ring, X off");
		check(cfg.fetch_mode == 3u && cfg.bitmap_width == 384u, "wide-X: fetch ancho (384)");
		check(cfg.map.wrap_x == 0u, "wide-X: X acotado");
	}

	// Pistas de video-splitting (tabla de la referencia: todos los XY* + YUnlimited2).
	check(playfield::variant_video_split(playfield::ScrollVariant::XYUnlimited2), "video-split en XYUnlimited2");
	check(playfield::variant_video_split(playfield::ScrollVariant::YUnlimited2), "video-split en YUnlimited2");
	check(playfield::variant_video_split(playfield::ScrollVariant::XYLimited), "video-split en XYLimited");
	check(!playfield::variant_video_split(playfield::ScrollVariant::XLimited), "XLimited sin video-split");
	check(!playfield::variant_video_split(playfield::ScrollVariant::YUnlimited), "YUnlimited sin video-split");

	// Puente a la tecnica del planner (scroll_plan.hpp).
	check(scene::scroll_kind_for_variant(playfield::ScrollVariant::XLimited) == scene::ScrollKind::CopperRing,
	      "bridge XLimited -> CopperRing");
	check(scene::scroll_kind_for_variant(playfield::ScrollVariant::XYLimited) == scene::ScrollKind::CopperSplit,
	      "bridge XYLimited -> CopperSplit (split)");
	check(scene::scroll_kind_for_variant(playfield::ScrollVariant::YUnlimited) == scene::ScrollKind::Fine,
	      "bridge YUnlimited -> Fine (sin split)");
	check(scene::scroll_kind_for_variant(playfield::ScrollVariant::XYUnlimited2) == scene::ScrollKind::CopperSplit,
	      "bridge XYUnlimited2 -> CopperSplit");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: seleccion de variantes de scroll (XLimited/XUnlimited/YUnlimited/XYLimited/XYUnlimited/_64/tall-Y/wide-X) validada.\n");
	return 0;
}
