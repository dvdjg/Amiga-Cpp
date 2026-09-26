// ============================================================================
// Test HOST-347: split vertical del anillo de scroll (eng::scene::ring_split) - F7.3.
// ============================================================================
//
// Respalda `eng/scene/scroll_plan.hpp`: `ring_split(scroll_y, visible_h, window_h)` describe como
// se muestra la vista sobre el anillo cuando cruza su final (que el driver materializa reapuntando
// BPLxPT en la linea del split). Base del materializador XYUnlimited/CopperSplit.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/347_ring_split

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
	std::printf("== HOST-347 ring_split ==\n");

	// Ventana 256 filas de anillo, vista de 240: sin wrap y con wrap.
	const auto a = eng::scene::ring_split(0u, 240u, 256u);
	check(!a.wrap && a.start_row == 0u && a.top_rows == 240u, "sin wrap");

	const auto b = eng::scene::ring_split(8u, 240u, 256u);
	check(!b.wrap && b.start_row == 8u && b.top_rows == 240u, "start 8, cabe entero");

	// start 20 + 240 = 260 > 256 -> wrap: primera parte 236 filas, resto desde 0.
	const auto c = eng::scene::ring_split(20u, 240u, 256u);
	check(c.wrap && c.start_row == 20u && c.top_rows == 236u, "wrap en 236");

	// scroll modulo window_h.
	const auto d = eng::scene::ring_split(300u, 240u, 256u);
	check(d.start_row == 44u, "scroll_y % window_h");

	// anillo 0 -> sin datos.
	const auto e = eng::scene::ring_split(10u, 100u, 0u);
	check(!e.wrap && e.start_row == 0u, "window 0");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: ring_split (split vertical del anillo) validado.\n");
	return 0;
}
