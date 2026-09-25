// ============================================================================
// Test HOST-329: actores en el mundo retenido (World + ActorStore + emit).
// ============================================================================
//
// Respalda la parte de actores de `eng/scene/world.hpp`: alta con la representación elegida
// por el engine (`reset_actors`/`add_actor`), acceso por `ActorId` y emisión al `FramePlan`
// con el destino y el clip (`emit`). Verifica que un actor BOB produce jobs de Blitter y
// que `reset_actors` limpia el mundo.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/329_world_actors

#include <cstdio>

#include <eng/scene/world.hpp>

namespace scene = eng::scene;
namespace graphics = eng::graphics;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

eng::u16 g_sheet[256] {};
eng::u16 g_mask[256] {};
eng::u16 g_dest[256] {};

scene::ActorDesc make_bob(eng::s16 x, eng::s16 y, eng::u8 z) {
	scene::ActorDesc d {};
	d.visual.kind = graphics::VisualKind::Bob;
	d.visual.pixels = eng::Span<const eng::u16> {g_sheet, 64u};
	d.visual.mask = eng::Span<const eng::u16> {g_mask, 64u};
	d.visual.w = 32u;
	d.visual.h = 16u;
	d.visual.bitplanes = 3u;
	d.layout = graphics::BobLayout::Planar;
	d.transparency = scene::TransparencyMode::Mask1Bit;
	d.background = scene::BackgroundPolicy::None;
	d.x = x;
	d.y = y;
	d.surface = 0u;
	d.z = z;
	d.preferred = scene::Representation::Bob;
	return d;
}

const graphics::BobTarget g_target {
	reinterpret_cast<eng::u8*>(g_dest), 8u, 64u, 4u, graphics::BobLayout::Planar};

} // namespace

int main() {
	std::printf("== HOST-329 world_actors ==\n");

	scene::World<2u, 4u> w;
	w.reset_actors(0, 60000u, 0u);

	const scene::ActorId a = w.add_actor(make_bob(16, 16, 10u));
	const scene::ActorId b = w.add_actor(make_bob(48, 16, 20u));
	check(a.valid() && b.valid(), "dos actores dados de alta");
	check(w.actors().count() == 2u, "el store cuenta 2");
	check(w.actor(a) != nullptr, "actor(a) accesible");
	check(w.actor(a)->desc.z == 10u, "z preservado");

	// Emision al plan: un BOB planar cookie-cut con mascara -> jobs.
	{
		graphics::FramePlan plan;
		const eng::u16 emitted = w.emit(
			plan, eng::Span<const graphics::BobTarget> {&g_target, 1u},
			graphics::DirtyRect {0, 0, 320, 256});
		check(emitted == 2u, "dos actores emitidos");
		check(plan.blit_job_count() >= 2u, "jobs de Blitter anadidos");
	}

	// reset_actors limpia el mundo de actores.
	w.reset_actors(0, 60000u, 0u);
	check(w.actors().count() == 0u, "reset_actors limpia");
	{
		graphics::FramePlan plan;
		check(w.emit(plan, eng::Span<const graphics::BobTarget> {&g_target, 1u},
			     graphics::DirtyRect {0, 0, 320, 256}) == 0u,
		      "mundo vacio -> 0 emitidos");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: World/actores (add/emit/reset) validado.\n");
	return 0;
}
