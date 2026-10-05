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

	// Mapeo variante (ScrollingTricks) -> ScrollKind: las variantes con video-split (las XY y
	// `YUnlimited2`) usan **CopperSplit** —el driver 8-way es el corkscrew `XlimitedScene`
	// (`y_mode=Ring`), demo 107; no hace falta un `XYUnlimited` aparte—; el resto de anillos X,
	// CopperRing. Es la via del consumidor NES (ROADMAP_API_COHERENCE §7.3).
	using eng::playfield::ScrollVariant;
	check(eng::scene::scroll_kind_for_variant(ScrollVariant::XYLimited) == ScrollKind::CopperSplit,
	      "XYLimited -> CopperSplit (corkscrew 8-way)");
	check(eng::scene::scroll_kind_for_variant(ScrollVariant::XYUnlimited) == ScrollKind::CopperSplit,
	      "XYUnlimited -> CopperSplit");
	check(eng::scene::scroll_kind_for_variant(ScrollVariant::XLimited) == ScrollKind::CopperRing,
	      "XLimited -> CopperRing");
	check(eng::scene::scroll_kind_for_variant(ScrollVariant::YUnlimited) == ScrollKind::Fine,
	      "YUnlimited -> Fine (sin split)");

	// Degradacion por presupuesto de Copper.
	check(eng::scene::choose_scroll(ScrollKind::CopperSplit, 4u) == ScrollKind::CopperSplit,
	      "cabe -> split");
	check(eng::scene::choose_scroll(ScrollKind::CopperSplit, 2u) == ScrollKind::CopperRing,
	      "degrada a ring");
	check(eng::scene::choose_scroll(ScrollKind::CopperSplit, 1u) == ScrollKind::Fine,
	      "degrada a fine");
	check(eng::scene::choose_scroll(ScrollKind::BlitterColumns, 0u) == ScrollKind::BlitterColumns,
	      "blitter columns no se degrada");

	// Estimacion de memoria (supuestos tipicos: 256x240, 3 planos, velocidad 4).
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

	// Interfaz de juego (`ScrollSpec`): la capa declara tecnica + periodo de mapa + velocidad y el
	// planner deriva el anillo. Para `Strip` toroidal el anillo es `visible + periodo` (el mapa
	// completo + una pantalla de solape), que es el dimensionado que evita el descuadre al envolver.
	const eng::scene::ScrollSpec strip {ScrollKind::Strip, 40u, 2u};
	check(strip.wraps(), "ScrollSpec toroidal: wraps()");
	check(eng::scene::scroll_ring_words(strip, 320u) == 60u,
	      "strip toroidal: anillo = visible(20) + periodo(40) = 60 words");
	const auto strip_mem = eng::scene::scroll_memory(strip, 320u, 256u, 3u);
	check(strip_mem.window_w == 960u && strip_mem.window_h == 256u,
	      "strip: ventana = anillo(60)*16 px x alto");
	check(strip_mem.bytes == 92160u, "strip: memoria = 120 B/fila-plano * 256 * 3");
	// Region toroidal con `Strip`: el planner usa la misma geometria y respeta el presupuesto.
	eng::scene::WorldRegion region {};
	region.top = 0; region.bottom = 256; region.planes = 3; region.speed_px = 2;
	region.scroll = ScrollKind::Strip; region.map_period_words = 40u;
	const auto plan_strip = eng::scene::plan_region(region, 320u, 256u, {0xffffu, 92160u});
	check(plan_strip.ok && plan_strip.scroll == ScrollKind::Strip, "plan_region strip cabe en Chip");
	check(plan_strip.memory.bytes == 92160u, "plan_region strip: memoria del anillo del mapa");
	const auto plan_tight = eng::scene::plan_region(region, 320u, 256u, {0xffffu, 92159u});
	check(plan_tight.scroll == ScrollKind::Fine, "plan_region strip: Chip ajustada -> degrada a fino");

	// Punto de entrada por `ScrollSpec` (sin `WorldRegion`): lo que usara el juego.
	const auto psc = eng::scene::plan_scroll(strip, 320u, 256u, 3u, {0xffffu, 92160u});
	check(psc.ok && psc.scroll == ScrollKind::Strip && psc.memory.bytes == 92160u,
	      "plan_scroll(ScrollSpec): strip cabe con la memoria del anillo del mapa");

	// La capa del mundo transporta su `ScrollSpec` (vocabulario de juego) sin conocer anillos.
	eng::scene::Layer layer {};
	check(layer.scroll() == ScrollKind::None && !layer.scroll_spec().wraps(),
	      "capa: scroll por defecto None");
	layer.set_scroll_spec({ScrollKind::Strip, 40u, 2u});
	check(layer.scroll() == ScrollKind::Strip && layer.scroll_spec().map_period_words == 40u &&
		      layer.scroll_spec().speed_px == 2u,
	      "capa: set_scroll_spec conserva tecnica, periodo y velocidad");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: scroll_plan (degradacion + memoria + ScrollSpec) validado.\n");
	return 0;
}
