// ============================================================================
// Test HOST-337: capa declarativa y regiones con tecnica generica - F4c(modelo).
// ============================================================================
//
// Respalda `eng/scene/world.hpp`: la tecnica de cada **region** es generica = **modo de
// display** (`SceneMode`)*) x **scroll** (`ScrollKind`)*) y declara su coste (`region_cost`).
// Asi una region puede ser un DPF con scroll por Copper, otra *copper-chunky* en los 48 px
// inferiores, o un playfield con scroll por columnas de Blitter (robocod) — sin que la capa
// elija hardware. El planner (F4c) valida/degrada con `region_cost`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/337_layer_plan

#include <cstdio>

#include <eng/scene/world.hpp>

namespace {

int g_fail = 0;
using Mode = eng::graphics::composition::SceneMode;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-337 layer_plan ==\n");

	eng::scene::World<4u> w;
	const auto l = w.add_layer("fondo", 0u);
	l->set_scroll(eng::scene::ScrollKind::BlitterColumns);
	l->set_prefer(eng::scene::LayerPlayfield::Pf2);
	check(l->scroll() == eng::scene::ScrollKind::BlitterColumns, "scroll pedido BlitterColumns");
	check(l->prefer() == eng::scene::LayerPlayfield::Pf2, "playfield preferido Pf2");

	// Regiones: DPF 208 px con scroll por Copper + banda copper-chunky de 48 px.
	const eng::scene::WorldRegion dpf {0u, 208u, eng::scene::LayerPlayfield::Pf1,
					   Mode::DualPlayfield, eng::scene::ScrollKind::CopperRing, 6u};
	const eng::scene::WorldRegion band {208u, 256u, eng::scene::LayerPlayfield::Pf1,
					    Mode::CopperChunky, eng::scene::ScrollKind::None, 0u};
	check(dpf.ok() && band.ok(), "regiones validas");
	check(w.add_region(dpf) && w.add_region(band), "add regiones");
	check(w.region_count() == 2u, "dos regiones");
	check(w.region(0)->mode == Mode::DualPlayfield, "region 0 = DPF");
	check(w.region(1)->mode == Mode::CopperChunky && w.region(1)->planes == 0u,
	      "region 1 = copper-chunky (sin bitplanes)");

	// Coste declarado de la tecnica (lo que el planner usara para aceptar/degradar).
	check(eng::scene::region_cost(Mode::CopperChunky, eng::scene::ScrollKind::None, 4u).planes == 0u,
	      "copper-chunky no consume planos");
	check(eng::scene::region_cost(Mode::Standard, eng::scene::ScrollKind::CopperSplit, 4u)
		      .copper_words_per_line == 4u,
	      "CopperSplit es caro en Copper");
	check(eng::scene::region_cost(Mode::Standard, eng::scene::ScrollKind::BlitterColumns, 4u)
		      .blitter_words_per_frame == 1u,
	      "BlitterColumns usa Blitter");
	check(eng::scene::region_cost(Mode::Standard, eng::scene::ScrollKind::Fine, 4u)
		      .copper_words_per_line == 0u,
	      "Fine no consume Copper por linea");

	// Region invalida (bottom <= top) se rechaza.
	const eng::scene::WorldRegion bad {208u, 208u, eng::scene::LayerPlayfield::Any, Mode::Standard,
					   eng::scene::ScrollKind::None, 0u};
	check(!w.add_region(bad), "region invalida rechazada");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: regiones con tecnica generica (modo x scroll + coste) validado.\n");
	return 0;
}
