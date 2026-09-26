// ============================================================================
// Test HOST-343: planificador de scroll adaptativo (eng::scene::scroll_plan) - F7.3.
// ============================================================================
//
// Respalda `eng/scene/scroll_plan.hpp`: eleccion/degradacion del scroll por presupuesto de Copper
// y estimacion de memoria de la ventana. Es la pieza que permite «la capa pide, el planner
// dispone» de forma adaptativa (XYUnlimited <-> XYLimited).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/343_scroll_plan

#include <cstdio>

#include <eng/scene/scroll_plan.hpp>

namespace {

int g_fail = 0;
using eng::scene::ScrollKind;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-343 scroll_plan ==\n");

	// Coste de Copper por linea.
	check(eng::scene::scroll_copper_per_line(ScrollKind::CopperSplit) == 4u, "split = 4");
	check(eng::scene::scroll_copper_per_line(ScrollKind::CopperRing) == 2u, "ring = 2");
	check(eng::scene::scroll_copper_per_line(ScrollKind::Fine) == 0u, "fine = 0");

	// Degradacion por presupuesto de Copper.
	check(eng::scene::choose_scroll(ScrollKind::CopperSplit, 4u) == ScrollKind::CopperSplit,
	      "cabe -> split");
	check(eng::scene::choose_scroll(ScrollKind::CopperSplit, 2u) == ScrollKind::CopperRing,
	      "degrada a ring");
	check(eng::scene::choose_scroll(ScrollKind::CopperSplit, 1u) == ScrollKind::Fine,
	      "degrada a fine");
	check(eng::scene::choose_scroll(ScrollKind::BlitterColumns, 0u) == ScrollKind::BlitterColumns,
	      "blitter columns no se degrada");

	// Estimacion de memoria (supuestos NES: 256x240, 3 planos, velocidad 4).
	const auto ring = eng::scene::scroll_memory(ScrollKind::CopperRing, 256u, 240u, 3u, 4u);
	check(ring.window_w == 280u && ring.window_h == 264u, "ventana xlimited = visible + 2*guarda");
	check(ring.bytes == 28512u, "memoria xlimited estimada");

	const auto split = eng::scene::scroll_memory(ScrollKind::CopperSplit, 256u, 240u, 3u, 4u);
	check(split.window_w == 272u && split.window_h == 256u, "ventana xyunlimited (ring +16)");

	// Eleccion con presupuesto de Chip.
	const ScrollKind ok = eng::scene::choose_scroll_fitting(ScrollKind::CopperSplit, 256u, 240u,
								3u, 4u, 4u, 100000u);
	check(ok == ScrollKind::CopperSplit, "cabe en Chip -> split");
	const ScrollKind tight = eng::scene::choose_scroll_fitting(ScrollKind::CopperSplit, 256u, 240u,
								   3u, 4u, 4u, 24000u);
	check(tight == ScrollKind::Fine, "Chip ajustada -> degrada a fine");
	const ScrollKind none =
		eng::scene::choose_scroll_fitting(ScrollKind::CopperSplit, 256u, 240u, 3u, 4u, 4u, 0u);
	check(none == ScrollKind::None, "sin Chip -> none");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: scroll_plan (degradacion + memoria) validado.\n");
	return 0;
}
