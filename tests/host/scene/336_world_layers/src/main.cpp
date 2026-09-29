// ============================================================================
// Test HOST-336: contenido de capa del mundo (actores vs tilemap) - F4b.
// ============================================================================
//
// Respalda `eng/scene/world.hpp`: una `Layer` del `World` puede ser de **actores** (por
// defecto) o de **tilemap** (contenido reusando `TileLayer`). Comprueba `add_layer`/
// `add_tile_layer`, `kind`, `is_tilemap` y el acceso al `TileLayer` de la capa.
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

	eng::scene::TileLayer tile {};
	tile.id = "mapa";
	tile.target_buffer = 1u;
	const auto fondo = w.add_tile_layer("fondo", 0u, tile);
	check(fondo.valid(), "capa de tilemap");
	check(fondo->kind() == eng::scene::WorldLayerKind::Tilemap, "kind Tilemap");
	check(fondo->is_tilemap(), "es tilemap");
	check(id_is(fondo->tilemap().id, "mapa"), "contenido TileLayer ligado");
	check(fondo->tilemap().target_buffer == 1u, "campos del TileLayer copiados");

	check(w.count() == 2u, "dos capas en el mundo");
	check(w.find("fondo").get() == fondo.get(), "find por id");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: World capas con contenido (actores vs tilemap) validado.\n");
	return 0;
}
