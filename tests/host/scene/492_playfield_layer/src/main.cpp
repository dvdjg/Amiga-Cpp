// ============================================================================
// Test HOST-492: capa PF (enemigo gigante) planificada como región, no como actor.
// ============================================================================
//
// Cubre:
//   - `plan_playfield_layer`: DPF/PF libre, PF2 explícito, `Any`, sin DPF, PF ocupado,
//     planos máximos y altura completa;
//   - una capa PF vive en `World`/`Layer` y no consume `ActorStore`;
//   - el vocabulario `ScenePlan` sigue eligiendo DPF para BG + FG en PF2.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/scene/492_playfield_layer

#include <cstdio>

#include <eng/scene/plan.hpp>
#include <eng/scene/playfield_layer.hpp>
#include <eng/scene/sprite_director.hpp>
#include <eng/scene/world.hpp>

namespace {

alignas(16) eng::u8 g_pf[320u * 64u / 8u];
alignas(16) eng::u16 g_giant[32];

int g_fail = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("[FAIL] %s\n", msg);
		++g_fail;
	}
}

void test_pf2_plan() {
	eng::scene::PlayfieldLayerCaps caps {};
	caps.dpf_enabled = true;
	caps.pf1_free = true;
	caps.pf2_free = true;
	caps.max_planes = 3u;

	eng::scene::PlayfieldLayerRequest req {};
	req.prefer = eng::scene::LayerPlayfield::Pf2;
	req.planes = 3u;
	req.full_height = true;
	auto plan = eng::scene::plan_playfield_layer(req, caps);
	check(plan.accepted && plan.field == eng::scene::LayerPlayfield::Pf2,
	      "PF2 aceptado con DPF y PF2 libre");

	req.planes = 4u;
	plan = eng::scene::plan_playfield_layer(req, caps);
	check(!plan.accepted, "4 planos no caben en DPF 3+3");

	req.planes = 3u;
	req.full_height = false;
	plan = eng::scene::plan_playfield_layer(req, caps);
	check(!plan.accepted, "una banda no se convierte en PF");

	caps.dpf_enabled = false;
	plan = eng::scene::plan_playfield_layer(req, caps);
	check(!plan.accepted, "sin DPF no hay PF2");
}

void test_preference_and_busy() {
	eng::scene::PlayfieldLayerCaps caps {};
	caps.dpf_enabled = true;
	caps.pf1_free = true;
	caps.pf2_free = false;
	caps.max_planes = 3u;

	eng::scene::PlayfieldLayerRequest req {};
	req.prefer = eng::scene::LayerPlayfield::Pf2;
	req.planes = 3u;
	auto plan = eng::scene::plan_playfield_layer(req, caps);
	check(!plan.accepted, "PF2 ocupado no cae silenciosamente a PF1");

	req.prefer = eng::scene::LayerPlayfield::Any;
	plan = eng::scene::plan_playfield_layer(req, caps);
	check(plan.accepted && plan.field == eng::scene::LayerPlayfield::Pf1,
	      "Any elige PF1 si PF2 está ocupado");

	caps.pf1_free = false;
	caps.pf2_free = true;
	plan = eng::scene::plan_playfield_layer(req, caps);
	check(plan.accepted && plan.field == eng::scene::LayerPlayfield::Pf2,
	      "Any prefiere PF2 cuando está libre");

	req.prefer = eng::scene::LayerPlayfield::Pf1;
	plan = eng::scene::plan_playfield_layer(req, caps);
	check(!plan.accepted, "PF1 explícito ocupado se rechaza");
}

void test_layer_is_not_actor() {
	eng::scene::World<2u, 2u, 2u> world;
	auto bg = world.add_layer("fondo", 0u);
	auto giant = world.add_layer("enemigo-gigante", 2u);
	check(bg.valid() && giant.valid(), "capas PF en World");
	giant->set_prefer(eng::scene::LayerPlayfield::Pf2);
	check(giant->prefer() == eng::scene::LayerPlayfield::Pf2, "preferencia PF2");
	check(giant->kind() == eng::scene::WorldLayerKind::Actors, "capa PF no es actor por sí misma");
	check(world.count() == 2u, "dos capas, sin ActorStore");
}

void test_director_rejects_layer_actor() {
	eng::graphics::Visual v {};
	v.pixels = eng::Span<const eng::u16> {g_giant, 32u};
	v.w = 32u;
	v.h = 8u;
	v.bitplanes = 1u;
	eng::scene::SpriteDirector<1u, 1u, 1u, 1u, 1u, 1u> director;
	const eng::scene::VisualId vid = director.add_visual(v);
	eng::scene::SpriteDesc d {};
	d.content = eng::scene::SpriteVisualRef::simple(vid);
	d.hint.policy = eng::scene::SpritePolicy::Layer;
	check(!director.add(d).valid(), "el director no convierte Layer en actor/BOB");
}

void test_scene_plan_dpf() {
	eng::scene::ScenePlan<2u> plan;
	check(plan.add(eng::scene::LayerRole::Background, eng::scene::LayerPlacement {}),
	      "BG en ScenePlan");
	eng::scene::LayerPlacement fg {};
	fg.field = 2u;
	check(plan.add(eng::scene::LayerRole::Foreground, fg, {},
		       eng::scene::LayerContent::Canvas),
	      "FG PF2 en ScenePlan");
	check(plan.strategy() == eng::scene::SceneStrategy::Dpf, "BG+FG PF2 = Dpf");
	check(plan.layer(1u).placement.field == 2u, "FG conserva PF2");
}

void test_layer_draw_cpu() {
	for (eng::usize i = 0u; i < sizeof(g_pf); ++i) {
		g_pf[i] = 0u;
	}
	for (eng::u16 i = 0u; i < 32u; ++i) {
		g_giant[i] = 0u;
	}
	// 32x8, un plano: fila compacta = 3 palabras (2 datos + guarda); arte a 0xffff.
	for (eng::u16 row = 0u; row < 8u; ++row) {
		g_giant[row * 3u] = 0xffffu;
		g_giant[row * 3u + 1u] = 0xffffu;
		g_giant[row * 3u + 2u] = 0u;
	}
	eng::scene::PlayfieldLayer layer {};
	layer.visual.pixels = eng::Span<const eng::u16> {g_giant, 32u};
	layer.visual.w = 32u;
	layer.visual.h = 8u;
	layer.visual.bitplanes = 1u;
	layer.x = 16;
	layer.y = 8;
	layer.field = eng::scene::LayerPlayfield::Pf2;

	eng::graphics::BobTarget target = eng::graphics::make_bob_target(
		eng::ChipView<eng::PlaneTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(g_pf), sizeof(g_pf)},
		40u, 64u, 1u, eng::graphics::BobLayout::Planar);
	check(layer.draw_cpu(target), "capa PF copiada por CPU");
	const eng::u16* row8 = reinterpret_cast<const eng::u16*>(g_pf + 8u * 40u);
	check(row8[0] == 0u && row8[1] == 0xffffu && row8[2] == 0xffffu,
	      "capa PF en x=16");
	check(layer.field == eng::scene::LayerPlayfield::Pf2, "capa PF2 sin ActorStore");
}

} // namespace

int main() {
	test_pf2_plan();
	test_preference_and_busy();
	test_layer_is_not_actor();
	test_layer_draw_cpu();
	test_director_rejects_layer_actor();
	test_scene_plan_dpf();
	if (g_fail == 0) {
		std::printf("OK: HOST-492 capa PF validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones.\n", g_fail);
	return 1;
}
