// ============================================================================
// Test HOST-336: contenido de capa del mundo (actores vs tilemap) - F4b.
// ============================================================================
//
// Respalda `eng/scene/world.hpp`: una `Layer` del `World` puede ser de actores, un fondo Fill
// o un tilemap (contenido reusando `TileLayer`). Comprueba las altas y el materializador Fill.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/336_world_layers

#include <cstdio>

#include <eng/scene/world.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

bool id_is(const char* a, const char* b) {
	if (a == nullptr || b == nullptr) {
		return false;
	}
	while (*a != '\0' && *a == *b) {
		++a;
		++b;
	}
	return *a == *b;
}

} // namespace

int main() {
	std::printf("== HOST-336 world_layers ==\n");

	eng::scene::World<4u> w;

	const auto actores = w.add_layer("objetos", 10u);
	check(actores.valid(), "capa de actores");
	check(actores->kind() == eng::scene::WorldLayerKind::Actors, "kind Actors");
	check(!actores->is_tilemap(), "no es tilemap");
	const auto fill = w.add_fill_layer("cielo", 1u, eng::Box {0, 0, 32u, 32u}, 3u);
	check(fill.valid() && fill->is_fill() && fill->fill_color() == 3u,
	      "capa Fill conserva geometría y color");
	const auto bad_fill = w.add_fill_layer("vacía", 0u, eng::Box {}, 0u);
	check(!bad_fill.valid(), "no se crea una región Fill vacía");
	eng::u8 emitted_colors[4] {};
	eng::u8 emitted = 0u;
	const bool materialized = w.materialize_fill_layers([&](const eng::Box&, eng::u8 color) {
		emitted_colors[emitted++] = color;
		return true;
	}, 32u, 32u);
	check(materialized && emitted == 2u && emitted_colors[0] == 0u && emitted_colors[1] == 3u,
	      "materializador Fill emite clear y color opaco");

	eng::scene::TileLayer tile {};
	tile.id = "mapa";
	tile.target_buffer = 1u;
	const auto fondo = w.add_tile_layer("fondo", 0u, tile);
	check(fondo.valid(), "capa de tilemap");
	check(fondo->kind() == eng::scene::WorldLayerKind::Tilemap, "kind Tilemap");
	check(fondo->is_tilemap(), "es tilemap");
	check(id_is(fondo->tilemap().id, "mapa"), "contenido TileLayer ligado");
	check(fondo->tilemap().target_buffer == 1u, "campos del TileLayer copiados");

	check(w.count() == 3u, "tres capas en el mundo");
	check(w.find("fondo").get() == fondo.get(), "find por id");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: World capas con contenido (actores vs tilemap) validado.\n");
	return 0;
}
