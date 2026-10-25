// HOST-498 — MapBundle v1 + MapAssetView + LayerView.
//
// Valida la cabecera big-endian, la tabla de capas, la paleta y el adaptador
// `TileMap` que consume el motor de scroll. El bundle es pequeño y está embebido
// para que el test no dependa de assets externos.

#include <eng/map/asset.hpp>

#include <cstdio>

namespace {

constexpr eng::u8 kBundle[] = {
	0x45, 0x4d, 0x41, 0x50, 0x00, 0x01, 0x00, 0x00, 0x10, 0x10, 0x02, 0x00,
	0x00, 0x01, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64,
	0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x58,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x0f, 0x00, 0x00, 0xf0, 0x00, 0x0f, 0x00, 0x01, 0x00, 0x02,
	0x00, 0x00, 0x00, 0x03,
};

[[nodiscard]] bool check(bool condition, const char* what) {
	if (!condition) std::printf("FALLO: %s\n", what);
	return condition;
}

} // namespace

int main() {
	using eng::map::LayerMode;
	using eng::map::LayerView;
	using eng::map::MapAssetView;

	const MapAssetView view {eng::Span<const eng::u8> {kBundle, sizeof(kBundle)}};
	bool ok = true;
	ok &= check(view.valid(), "valid");
	ok &= check(view.version() == 1u, "version");
	ok &= check(view.tile_width() == 16u && view.tile_height() == 16u, "tile size");
	ok &= check(view.planes() == 2u, "planes");
	ok &= check(view.layer_count() == 1u, "layer_count");
	ok &= check(view.palette_count() == 4u, "palette_count");
	ok &= check(view.tile_count() == 4u, "tile_count");
	ok &= check(view.bank_bytes() == 100u, "bank_bytes");
	ok &= check(view.palette_word(0u) == 0x0000u, "palette[0]");
	ok &= check(view.palette_word(1u) == 0x0f00u, "palette[1]");
	ok &= check(view.palette_word(2u) == 0x00f0u, "palette[2]");
	ok &= check(view.palette_word(3u) == 0x000fu, "palette[3]");

	const MapAssetView::Layer layer = view.layer(0u);
	ok &= check(layer.id == 7u, "layer id");
	ok &= check(layer.mode == LayerMode::Baked, "layer mode");
	ok &= check(layer.width == 2u && layer.height == 2u, "layer size");
	ok &= check(layer.wrap_x == 2u && layer.wrap_y == 0u, "layer wrap");
	ok &= check(layer.empty_tile == 0xffffu, "layer empty");
	ok &= check(layer.cell(0u, 0u) == 1u && layer.cell(1u, 0u) == 2u, "layer cells x");
	ok &= check(layer.cell(0u, 1u) == 0u && layer.cell(1u, 1u) == 3u, "layer cells y");

	static_assert(eng::playfield::TileMap<LayerView>, "LayerView cumple TileMap");
	const LayerView map {view, 0u};
	ok &= check(map.has_data(), "LayerView has_data");
	ok &= check(map.width == 2u && map.height == 2u, "LayerView geometry");
	ok &= check(map.tile_at(0, 0) == 1u, "tile_at 0,0");
	ok &= check(map.tile_at(1, 0) == 2u, "tile_at 1,0");
	ok &= check(map.tile_at(2, 0) == 1u, "wrap x");
	ok &= check(map.tile_at(-1, 0) == 2u, "wrap x negative");
	ok &= check(map.tile_at(0, 1) == 0u, "tile_at 0,1");
	ok &= check(map.tile_at(1, 1) == 3u, "tile_at 1,1");
	ok &= check(map.tile_at(0, 2) == map.edge_tile, "bounded y");
	ok &= check(map.is_empty(0xffffu) && !map.is_empty(0u), "empty sentinel");

	eng::u8 bad[sizeof(kBundle)] {};
	for (eng::usize i = 0u; i < sizeof(kBundle); ++i) bad[i] = kBundle[i];
	bad[0] = 'X';
	const MapAssetView invalid {eng::Span<const eng::u8> {bad, sizeof(bad)}};
	ok &= check(!invalid.valid(), "magic inválido");
	ok &= check(!LayerView {invalid, 0u}.has_data(), "LayerView inválida");

	if (!ok) {
		std::printf("HOST-498 FALLO\n");
		return 1;
	}
	std::printf("HOST-498 OK: MapBundle v1, capa baked, paleta y TileMap validados.\n");
	return 0;
}
