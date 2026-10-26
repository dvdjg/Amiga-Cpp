#include <eng/field/tile_layer_stack.hpp>

#include <cstdio>

using eng::playfield::TileLayerBinding;
using eng::playfield::TileLayerMode;
using eng::playfield::TileLayerStack;

int main() {
	const TileLayerBinding layers[] = {
		{0u, TileLayerMode::Baked, true, 0u},
		{1u, TileLayerMode::AnimatedOverlay, false, 0u},
		{2u, TileLayerMode::Occluder, false, 128u},
		{3u, TileLayerMode::Skip, false, 64u},
	};
	TileLayerStack stack {};
	if (stack.configured()) {
		std::printf("FAIL: una pila vacía no debe estar configurada\n");
		return 1;
	}
	stack.configure(eng::Span<const TileLayerBinding> {layers, 4u}, 0u);
	if (!stack.configured() || stack.baked_through() != 0u) {
		std::printf("FAIL: configure/baked_through\n");
		return 1;
	}
	if (stack.live_count() != 2u) {
		std::printf("FAIL: live_count=%u\n", static_cast<unsigned>(stack.live_count()));
		return 1;
	}
	if (!stack.is_live(1u) || !stack.is_live(2u) || stack.is_live(0u) ||
	    stack.is_live(3u) || stack.is_live(9u)) {
		std::printf("FAIL: is_live\n");
		return 1;
	}
	if (!TileLayerStack::cell_intersects(16u, 32u, eng::graphics::DirtyRect {0, 16, 40, 56})) {
		std::printf("FAIL: interseccion de dirty rect\n");
		return 1;
	}
	if (TileLayerStack::cell_intersects(16u, 32u, eng::graphics::DirtyRect {32, 48, 64, 80})) {
		std::printf("FAIL: dirty rect no interseca\n");
		return 1;
	}
	if (layers[2].priority != 128u || layers[3].priority != 64u) {
		std::printf("FAIL: prioridad\n");
		return 1;
	}
	std::printf("OK: TileLayerStack modos/prioridades/dirty rect\n");
	return 0;
}