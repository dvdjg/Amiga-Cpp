// ============================================================================
// Test HOST-346: geometria del anillo de scroll (eng::scene::scroll_ring) - F7.3.
// ============================================================================
//
// Respalda `eng/scene/scroll_plan.hpp`: `scroll_ring` (ventana = visible + 2*margen, bytes por
// plano y total) y `ring_crossed` (bandas de tile cruzadas al desplazar). Es la base de los
// drivers de scroll con guardas.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/346_scroll_ring

#include <cstdio>

#include <eng/scene/scroll_plan.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-346 scroll_ring ==\n");

	// Ventana 256x240 con margen de 8 en ambos ejes, 3 planos.
	const auto r = eng::scene::scroll_ring(256u, 240u, 3u, 8u, 8u);
	check(r.window_w == 272u && r.window_h == 256u, "ventana = visible + 2*margen");
	check(r.row_bytes == 34u, "row_bytes 272 px (34 B)");
	check(r.bytes == 34u * 256u * 3u, "bytes totales");

	// Sin margen = ventana visible.
	const auto r0 = eng::scene::scroll_ring(256u, 240u, 2u, 0u, 0u);
	check(r0.window_w == 256u && r0.window_h == 240u, "sin margen");

	// Bandas de tile cruzadas (tile 16).
	check(eng::scene::ring_crossed(0u, 4u, 16u) == 0u, "4 px no cruza tile");
	check(eng::scene::ring_crossed(12u, 20u, 16u) == 1u, "cruza 1 tile");
	check(eng::scene::ring_crossed(0u, 40u, 16u) == 2u, "cruza 2 tiles");
	check(eng::scene::ring_crossed(40u, 4u, 16u) == 2u, "sentido inverso = abs");
	check(eng::scene::ring_crossed(0u, 40u, 0u) == 0u, "tile 0 -> 0");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: scroll_ring (geometria + bandas cruzadas) validado.\n");
	return 0;
}
