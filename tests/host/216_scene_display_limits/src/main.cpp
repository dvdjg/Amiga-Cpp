// ============================================================================
// Test HOST-216: `scene/limits.hpp` — perfiles, validacion y coste de bus (DmaCost).
// ============================================================================
//
// Cubre, sin hardware:
//
//   1) Perfiles de capacidades: `ocs_a500`/`ecs` (6 planos, fetch 1x) y `aga_a1200`
//      (8 planos, HAM8=`max_planes_ham=8`, DPF 4+4, fetch 4x).
//   2) `dma_cost`: palabras de fetch × planos / ancho de fetch, con los slots fijos (27)
//      y usables (226) de `dma_architecture.md`. Valores fijados a 320 px.
//   3) `valid_fetch_width`: OCS/ECS no admiten 2x/4x; AGA si.
//   4) `validate`/`valid_scene`: codigos de rechazo (1 ancho, 3 visible, 5 planos, 9 modo)
//      y aceptacion por modo (HAM6/HAM8, DPF, planos > 6 solo AGA).
//   5) `geometry_for`: la geometria derivada de 320x256 es `kPal320x256`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/216_scene_display_limits   (solo este)

#include <cstdio>

#include <eng/core/types.hpp>
#include <eng/graphics/scene/compose.hpp>
#include <eng/graphics/scene/limits.hpp>

namespace {

using namespace eng::graphics::scene;

constexpr SceneResources ham6() {
	SceneResources r = planar(320, 256, 6);
	r.mode = SceneMode::Ham;
	return r;
}

constexpr SceneResources ham8() {
	SceneResources r = planar(320, 256, 8);
	r.mode = SceneMode::Ham;
	return r;
}

constexpr SceneResources dpf4() {
	SceneResources r = planar(320, 256, 4);
	r.mode = SceneMode::DualPlayfield;
	return r;
}

// --- Perfiles ------------------------------------------------------------------------
static_assert(ocs_a500.slots_per_line == 226u && ocs_a500.fixed_dma_slots == 27u);
static_assert(ocs_a500.fetch_width_max == 1u && ocs_a500.max_planes == 6u);
static_assert(ecs.slots_per_line == 226u && ecs.max_planes == 6u && ecs.fetch_width_max == 1u);
static_assert(aga_a1200.max_planes == 8u && aga_a1200.max_planes_ham == 8u);
static_assert(aga_a1200.max_planes_dpf == 4u && aga_a1200.fetch_width_max == 4u);

// --- Ancho de fetch ------------------------------------------------------------------
static_assert(!valid_fetch_width(ocs_a500, FetchWidth::X2));
static_assert(!valid_fetch_width(ecs, FetchWidth::X4));
static_assert(valid_fetch_width(aga_a1200, FetchWidth::X4));

// --- Coste de bus (320 px = 20 palabras de fetch por plano) --------------------------
static_assert(dma_cost(planar(320, 256, 4), ocs_a500).fetch_words == 20u);
static_assert(dma_cost(planar(320, 256, 4), ocs_a500).bitplane_slots == 80u);
static_assert(dma_cost(planar(320, 256, 4), ocs_a500).used_slots == 107u);
static_assert(dma_cost(planar(320, 256, 4), ocs_a500).cpu_slots == 119u);
static_assert(dma_cost(planar(320, 256, 6), ocs_a500).bitplane_slots == 120u);
static_assert(dma_cost(planar(320, 256, 6), ocs_a500).cpu_slots == 79u);
static_assert(dma_cost(planar(320, 256, 2), ocs_a500).cpu_slots == 159u);
static_assert(dma_cost(planar(320, 256, 8), aga_a1200, FetchWidth::X1).bitplane_slots == 160u);
static_assert(dma_cost(planar(320, 256, 8), aga_a1200, FetchWidth::X1).cpu_slots == 39u);
static_assert(dma_cost(planar(320, 256, 8), aga_a1200, FetchWidth::X4).bitplane_slots == 40u);
static_assert(dma_cost(planar(320, 256, 8), aga_a1200, FetchWidth::X4).cpu_slots == 159u);
// `fw` por encima del perfil se acota al maximo (OCS 4x -> 1x).
static_assert(dma_cost(planar(320, 256, 4), ocs_a500, FetchWidth::X4).bitplane_slots == 80u);

// --- Validacion ----------------------------------------------------------------------
static_assert(valid_scene(planar(320, 256, 4), ocs_a500));
static_assert(valid_scene(planar(320, 256, 6), ecs));
static_assert(validate(planar(300, 256, 4), ocs_a500).code == 1u); // no multiplo de 16
static_assert(validate(planar(384, 256, 4), ocs_a500).code == 3u); // > 368 visibles
static_assert(validate(planar(320, 256, 7), ocs_a500).code == 5u); // 7 planos exigen AGA
static_assert(valid_scene(planar(320, 256, 8), aga_a1200));
static_assert(valid_scene(ham6(), ocs_a500));
static_assert(validate(ham8(), ocs_a500).code == 5u); // 8 planos no caben en OCS (max 6)
static_assert(valid_scene(ham8(), aga_a1200));
static_assert(validate(dpf4(), ocs_a500).code == 9u); // DPF OCS = 3+3
static_assert(valid_scene(dpf4(), aga_a1200));

// --- Geometria derivada --------------------------------------------------------------
static_assert(geometry_for(planar(320, 256, 4)).ddfstrt == kPal320x256.ddfstrt);
static_assert(geometry_for(planar(320, 256, 4)).ddfstop == kPal320x256.ddfstop);

} // namespace

int main() {
	std::printf("OK: scene::limits — perfiles OCS/ECS/AGA, DmaCost (fetch 1x/2x/4x), validacion y geometria.\n");
	return 0;
}
