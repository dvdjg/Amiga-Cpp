// ============================================================================
// Test HOST-335: descriptor único de objeto (Sprite <-> Visual/ActorDesc).
// ============================================================================
//
// Respalda la convergencia del sistema de objetos: un `Sprite` (BOB cocinado, camino
// `Screen::sprite`) expone su **vista de dominio** `Visual` y se convierte en `ActorDesc`
// (camino `World::add_actor`) con `actor_desc_from_sprite`. Así ambas vías usan el mismo asset
// y no hay dos descriptores distintos. Fija también que `Visual` transporta los frames.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/335_sprite_actor

#include <cstdio>

#include <eng/graphics/bob.hpp>
#include <eng/graphics/sprite_asset.hpp>
#include <eng/scene/actor_types.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

eng::u8 g_sheet[192] {};
eng::u8 g_mask[96] {};

} // namespace

int main() {
	std::printf("== HOST-335 sprite_actor ==\n");

	eng::graphics::Bob b {};
	b.sheet = g_sheet;
	b.mask = g_mask;
	b.width = 32u;
	b.height = 16u;
	b.planes = 2u;
	b.layout = eng::graphics::BobLayout::Planar;
	b.draw = eng::graphics::BobDraw::CookieCut;
	b.frame_count = 3u;
	b.frame_stride = 64u;

	const eng::graphics::Sprite spr {b, sizeof(g_sheet), sizeof(g_mask)};

	// Vista de dominio (mismo asset).
	const eng::graphics::Visual v = spr.visual();
	check(v.kind == eng::graphics::VisualKind::Bob, "kind Bob");
	check(v.w == 32u && v.h == 16u && v.bitplanes == 2u, "geometria");
	check(v.pixels.size() == sizeof(g_sheet) / 2u, "pixels dimensionado por sheet_bytes");
	check(v.mask.size() == sizeof(g_mask) / 2u, "mascara dimensionada por mask_bytes");
	check(v.frame_count == 3u && v.frame_stride == 64u, "frames transportados al Visual");

	// Conversion al camino de actores.
	const eng::scene::ActorDesc d = eng::scene::actor_desc_from_sprite(spr, 10, 20, 5u);
	check(d.visual.pixels.size() == sizeof(g_sheet) / 2u, "ActorDesc usa el mismo asset");
	check(d.layout == eng::graphics::PlaneLayout::Planar, "layout propagado");
	check(d.transparency == eng::scene::TransparencyMode::Mask1Bit, "transparencia por defecto");
	check(d.x == 10 && d.y == 20 && d.z == 5u, "posicion/z");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Sprite<->Visual/ActorDesc (descriptor unico) validado.\n");
	return 0;
}
