// ============================================================================
// Test HOST-345: planner de region (eng::scene::plan_region) - F4c (decision).
// ============================================================================
//
// Respalda `eng/scene/scroll_plan.hpp`: `plan_region(WorldRegion, visible, budget)` elige el
// `ScrollKind` efectivo (degradando por Copper/Chip), con su coste y su memoria. Cierra el
// modelo «la capa pide, el planner dispone» a nivel de region.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/345_region_plan

#include <cstdio>

#include <eng/scene/scroll_plan.hpp>

namespace {

int g_fail = 0;
using Mode = eng::graphics::composition::SceneMode;
using eng::scene::LayerPlayfield;
using eng::scene::ScrollKind;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-345 region_plan ==\n");

	const eng::scene::WorldRegion dpf {0u, 208u, LayerPlayfield::Pf1, Mode::DualPlayfield,
					   ScrollKind::CopperSplit, 6u, 4u};

	// Presupuesto holgado: se respeta la peticion.
	const auto full = eng::scene::plan_region(dpf, 256u, 208u, {4u, 0xffffffffu});
	check(full.ok && full.scroll == ScrollKind::CopperSplit, "cabe -> CopperSplit");
	check(full.cost.copper_words_per_line == 4u, "coste de Copper");
	check(full.memory.window_w == 272u && full.memory.window_h == 224u, "ventana xyunlimited");

	// Copper ajustado: degrada a ring.
	const auto ring = eng::scene::plan_region(dpf, 256u, 208u, {2u, 0xffffffffu});
	check(ring.ok && ring.scroll == ScrollKind::CopperRing, "Copper ajustado -> CopperRing");
	check(ring.memory.window_w == 280u, "ventana xlimited (guarda = speed+8)");

	// Chip ajustado (cabe Fine, no Ring): degrada mas (a Fine, sin ventana extra).
	const auto fine = eng::scene::plan_region(dpf, 256u, 208u, {4u, 45000u});
	check(fine.ok && fine.scroll == ScrollKind::Fine, "Chip ajustado -> Fine");
	check(fine.memory.window_w == 256u, "Fine = ventana visible");

	// Sin presupuesto: no cabe.
	const auto none = eng::scene::plan_region(dpf, 256u, 208u, {4u, 0u});
	check(!none.ok, "sin Chip -> no cabe");
	check(none.scroll == ScrollKind::None, "scroll None");

	// Region estatica (pide None): siempre ok.
	const eng::scene::WorldRegion stat {0u, 208u, LayerPlayfield::Pf1, Mode::Standard,
					    ScrollKind::None, 4u, 0u};
	check(eng::scene::plan_region(stat, 256u, 208u, {0u, 0u}).ok, "region estatica ok");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: plan_region (scroll efectivo + coste + memoria) validado.\n");
	return 0;
}
