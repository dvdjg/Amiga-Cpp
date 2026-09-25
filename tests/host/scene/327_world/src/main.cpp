// ============================================================================
// Test HOST-327: mundo retenido (eng::scene::World / Layer).
// ============================================================================
//
// Respalda `eng/scene/world.hpp`: el contenedor aditivo de capas (`Layer`) con su cámara.
// Comprueba `add_layer` (con rechazo anulable al llenarse), `count`/`full`, `layer`/`find`
// por índice/id, y que la cámara de la capa responde a `scroll_x`/`scroll_y`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/327_world

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
	std::printf("== HOST-327 world ==\n");

	eng::scene::World<2> w;
	check(w.count() == 0u && !w.full(), "mundo vacio");
	check(w.capacity() == 2u, "capacidad");

	const auto fondo = w.add_layer("fondo", 0u);
	const auto sprites = w.add_layer("sprites", 2u);
	check(fondo.valid() && sprites.valid(), "dos capas anadidas");
	check(w.count() == 2u && w.full(), "mundo lleno");

	// Sin fallo silencioso: la tercera capa devuelve un Ref invalido.
	check(!w.add_layer("hud", 1u).valid(), "lleno -> Ref invalido");

	check(id_is(fondo->id(), "fondo") && fondo->depth() == 0u, "id/depth de la primera capa");
	check(id_is(sprites->id(), "sprites") && sprites->depth() == 2u, "id/depth de la segunda");

	// Busqueda por indice e id.
	check(w.layer(0u) == fondo.get(), "layer(0) = primera");
	check(w.layer(9u) == nullptr, "layer fuera de rango -> nullptr");
	check(w.find("sprites") == sprites.get(), "find por id");
	check(w.find("nope") == nullptr, "find inexistente -> nullptr");

	// Camara de la capa: scroll recortado al mundo.
	fondo->camera().reset(eng::scene::WorldRect {0u, 0u, 640u, 256u}, eng::Size2u {320u, 256u});
	check(fondo->camera().scroll_x() == 0u, "scroll inicial = 0");
	fondo->camera().set_scroll_x(128u);
	check(fondo->camera().scroll_x() == 128u, "set_scroll_x");
	fondo->camera().set_scroll_x(1000u); // se recorta a 640-320 = 320
	check(fondo->camera().scroll_x() == 320u, "scroll recortado al mundo");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: World/Layer (add/count/full/layer/find, camara+scroll) validado.\n");
	return 0;
}
