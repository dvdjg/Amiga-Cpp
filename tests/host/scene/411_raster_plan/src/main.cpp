// ============================================================================
// Test HOST-411: convergencia planner ↔ RasterLayout (etapa §7)
// ============================================================================
//
// Verifica `eng::scene::plan_raster_layout`: desde un `ScenePlan` + una vista por capa construye un
// `RasterLayout` (1 banda Single, 1 banda dual Dpf, N bandas Bands) o rechaza. Puro.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/scene/411_raster_plan

#include <cstdio>

#include <eng/scene/raster_plan.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

using eng::scene::LayerPlacement;
using eng::scene::LayerRole;
using eng::scene::plan_raster_layout;
using eng::scene::RasterLayout;
using eng::scene::RasterPlanError;
using eng::scene::ScenePlan;

eng::playfield::PlayfieldHardwareView dummy_view(eng::u8* base, eng::u16 top_ignored = 0u) {
	(void)top_ignored;
	eng::playfield::PlayfieldHardwareView v {};
	v.planes = 3u;
	v.bitmap_bytes_per_row = 80u;
	v.bitmap_height = 256u;
	v.viewport_w = 320u;
	v.viewport_h = 256u;
	v.display_height = 256u;
	v.viewport_h = 256u;
	v.plane_bytes = 80u * 256u * 3u;
	v.real_base = eng::Address<eng::MemoryKind::Chip>::from_storage(base);
	v.bpl1mod = static_cast<eng::u16>(80u * 3u - 40u);
	v.bpl2mod = v.bpl1mod;
	return v;
}

void test_single() {
	alignas(16) eng::u8 buf[256] {};
	const eng::playfield::PlayfieldHardwareView views[1] = {dummy_view(buf)};
	ScenePlan<4u> plan {};
	(void)plan.add(LayerRole::Background, LayerPlacement {});
	RasterLayout layout {};
	const auto n = plan_raster_layout(plan, eng::Span<const eng::playfield::PlayfieldHardwareView> {views, 1u}, layout);
	check(n.has_value() && *n == 1u && layout.count() == 1u, "Single → 1 banda");
}

void test_dpf_and_bands() {
	alignas(16) eng::u8 buf[256] {};
	const eng::playfield::PlayfieldHardwareView views[2] = {dummy_view(buf), dummy_view(buf)};

	ScenePlan<4u> dpf {};
	(void)dpf.add(LayerRole::Background, LayerPlacement {});
	(void)dpf.add(LayerRole::Foreground, LayerPlacement {});
	RasterLayout l1 {};
	const auto n1 = plan_raster_layout(dpf, eng::Span<const eng::playfield::PlayfieldHardwareView> {views, 2u}, l1);
	check(n1.has_value() && *n1 == 1u && l1.count() == 1u && l1[0].dual_playfield, "Dpf → 1 banda dual");

	ScenePlan<4u> bands {};
	(void)bands.add(LayerRole::Foreground, LayerPlacement {0u, 128u, 0u});
	(void)bands.add(LayerRole::Foreground, LayerPlacement {128u, 128u, 0u});
	RasterLayout l2 {};
	const auto n2 = plan_raster_layout(bands, eng::Span<const eng::playfield::PlayfieldHardwareView> {views, 2u}, l2);
	check(n2.has_value() && *n2 == 2u && l2.count() == 2u, "Bands → 2 bandas");
	check(l2[1].top == 128u, "banda 1 en top=128");
}

void test_errors() {
	const eng::playfield::PlayfieldHardwareView views[1] = {};
	ScenePlan<4u> dpf {};
	(void)dpf.add(LayerRole::Background, LayerPlacement {});
	(void)dpf.add(LayerRole::Foreground, LayerPlacement {});
	RasterLayout layout {};
	check(plan_raster_layout(dpf, eng::Span<const eng::playfield::PlayfieldHardwareView> {views, 1u}, layout)
		      .error() == RasterPlanError::NoViews,
	      "faltan vistas → NoViews");

	ScenePlan<4u> empty {};
	check(plan_raster_layout(empty, eng::Span<const eng::playfield::PlayfieldHardwareView> {}, layout)
		      .error() == RasterPlanError::UnsupportedStrategy,
	      "plan vacío → UnsupportedStrategy");
}

} // namespace

int main() {
	test_single();
	test_dpf_and_bands();
	test_errors();
	if (failures == 0) {
		std::printf("OK: convergencia planner/RasterLayout (plan_raster_layout) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
