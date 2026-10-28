// ============================================================================
// Test HOST-501: sprite de objeto de mapa no alineado y su animación.
// ============================================================================
//
// Valida sin emulador que `map_object_sprite` construye un `Sprite` planar con
// el layout de `object_sheet.bin` (guarda por fila) y que `object_frame_at_tick`
// recorre las duraciones de la tabla generada.

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>
#include <eng/map/objects.hpp>

namespace {

using eng::u8;
using eng::u16;
using eng::u32;
using eng::graphics::Sprite;
using eng::map::MapObjectAnimationView;
using eng::map::MapObjectImageView;
using eng::map::map_object_sprite;
using eng::map::object_frame_at_tick;

alignas(16) eng::u8 g_sheet[512] {};
alignas(16) eng::u8 g_mask[128] {};

int g_failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::printf("FALLO: %s\n", what);
		++g_failures;
	}
}

} // namespace

int main() {
	const MapObjectImageView image {0u, 0u, 16u, 16u, 2u, 0u};
	const MapObjectAnimationView anim {128u, 2u, 0u, {5u, 8u, 0u, 0u, 0u, 0u, 0u, 0u}};
	const eng::ChipView<eng::BobTag> sheet {
		eng::Address<eng::MemoryKind::Chip>::from_storage(g_sheet), sizeof(g_sheet)};
	const eng::ChipView<eng::BobTag> mask {
		eng::Address<eng::MemoryKind::Chip>::from_storage(g_mask), sizeof(g_mask)};
	const Sprite sprite = map_object_sprite(image, anim, 4u, sheet, mask);
	check(sprite.valid(), "sprite: valido");
	check(sprite.width() == 16u && sprite.height() == 16u, "sprite: geometria");
	check(sprite.frames() == 2u, "sprite: dos frames");
	check(sprite.bob().frame_stride == 256u, "sprite: stride de frame");
	check(object_frame_at_tick(anim, 0u) == 0u, "anim: tick 0");
	check(object_frame_at_tick(anim, 4u) == 0u, "anim: tick 4");
	check(object_frame_at_tick(anim, 5u) == 1u, "anim: tick 5");
	check(object_frame_at_tick(anim, 12u) == 1u, "anim: tick 12");
	check(object_frame_at_tick(anim, 13u) == 0u, "anim: wrap");
	if (g_failures != 0) {
		std::printf("HOST-501: %d fallo(s)\n", g_failures);
		return 1;
	}
	std::printf("HOST-501: sprite de objeto animado OK\n");
	return 0;
}
