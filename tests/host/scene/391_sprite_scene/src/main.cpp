// ============================================================================
// Test HOST-391: fachada de sprites de nivel A (`eng::SpriteScene`) - F7.7.
// ============================================================================
//
// Respalda `eng/api/sprites.hpp`: el juego da de alta **actores** (`ActorDesc`) y llama `emit`, y
// el engine compone (HW sprites + `SpriteAllocator` + BOB fallback) sin que el juego declare los
// buffers de trabajo ni un `FramePlan`. Los actores que no caben en sprite hardware se **degradan**
// (`result.degraded`) o se dibujan como BOB.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/391_sprite_scene

#include <cstdio>

#include <eng/api/sprites.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

/// Hoja 16x16 a 2 planos en memoria host (escape documentado del test: en Amiga sería Chip).
eng::u16 g_sheet[16u * 2u] {};

} // namespace

int main() {
	std::printf("== HOST-391 sprite_scene ==\n");

	eng::SpriteScene<4u> sp {};
	// 2 canales de sprite hardware, sin capas, con presupuesto de BOB: 2 actores caben como sprite,
	// el resto degrada a BOB.
	sp.set_budget(eng::scene::RepresentationBudget {2u, 60000u, 0u});

	for (eng::usize i = 0u; i < 32u; ++i) {
		g_sheet[i] = 0xffffu;
	}
	eng::graphics::Visual v {};
	v.kind = eng::graphics::VisualKind::Bob;
	v.pixels = eng::Span<const eng::u16> {g_sheet, 32u};
	v.w = 16u;
	v.h = 16u;
	v.bitplanes = 2u;
	v.frame_count = 1u;
	v.frame_stride = 0u;

	eng::scene::ActorDesc d {};
	d.visual = v;
	d.x = 10;
	d.y = 20;
	d.transparency = eng::scene::TransparencyMode::Mask1Bit;
	d.layout = eng::graphics::BobLayout::Planar;

	check(sp.add(d).valid(), "add (1)");
	check(sp.count() == 1u, "count = 1");
	// Un actor sin contenido se rechaza (no crece el conteo).
	eng::scene::ActorDesc bad {};
	check(!sp.add(bad).valid(), "add sin visual -> invalido");
	check(sp.count() == 1u, "count sigue 1");

	eng::u16 bitmap[320u * 8u] {};
	eng::graphics::FramePlan plan {};
	const eng::graphics::BobTarget target = eng::graphics::make_bob_target(
		eng::MemView<eng::PlaneTag, eng::MemoryKind::Chip> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(bitmap), sizeof(bitmap)},
		40u, 8u, 2u, eng::graphics::BobLayout::Planar);
	const eng::graphics::BobTarget targets[1] = {target};
	eng::scene::ActorEmitContext ctx {};
	ctx.targets = eng::Span<const eng::graphics::BobTarget> {targets, 1u};
	ctx.display_top = 0x2cu;

	const auto r = sp.emit(plan, ctx);
	check(r.ok, "emit ok");
	check(r.sprites + r.degraded == 1u, "1 actor materializado (sprite o degradado)");
	check(sp.placements().size() == r.sprites, "placements == sprites");

	check(sp.remove(sp.store().id_at(0u)), "remove");
	check(sp.count() == 0u, "count = 0 tras remove");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: SpriteScene (alta + compose + degrade) validada.\n");
	return 0;
}
