// ============================================================================
// Test HOST-337: capa declarativa (scroll/prefer) y regiones del mundo - F4c(modelo).
// ============================================================================
//
// Respalda `eng/scene/world.hpp`: una `Layer` **pide** scroll (`LayerScroll`) y playfield
// preferido (`LayerPlayfield`), y el `World` guarda **regiones** verticales (`WorldRegion`
// con top/bottom/playfield/modo/planos), lo que permite expresar un DPF + una banda de otra
// altura. El planner (F4c) es quien valida/materializa/degrada; aquí se fija el modelo.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/337_layer_plan

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

} // namespace

int main() {
	std::printf("== HOST-337 layer_plan ==\n");

	eng::scene::World<4u> w;
	const auto l = w.add_layer("fondo", 0u);
	l->set_scroll(eng::scene::LayerScroll::XLimited);
	l->set_prefer(eng::scene::LayerPlayfield::Pf2);
	check(l->scroll() == eng::scene::LayerScroll::XLimited, "scroll pedido XLimited");
	check(l->prefer() == eng::scene::LayerPlayfield::Pf2, "playfield preferido Pf2");

	// Regiones: DPF 208 px + banda de 48 px (por Copper en top=208).
	const eng::scene::WorldRegion dpf {
		0u, 208u, eng::scene::LayerPlayfield::Pf1,
		eng::graphics::composition::SceneMode::DualPlayfield, 6u};
	const eng::scene::WorldRegion band {
		208u, 256u, eng::scene::LayerPlayfield::Pf1,
		eng::graphics::composition::SceneMode::Standard, 4u};
	check(dpf.ok() && band.ok(), "regiones validas");
	check(w.add_region(dpf), "add DPF");
	check(w.add_region(band), "add banda");
	check(w.region_count() == 2u, "dos regiones");
	check(w.region(0)->mode == eng::graphics::composition::SceneMode::DualPlayfield,
	      "region 0 = DPF");
	check(w.region(1)->planes == 4u && w.region(1)->top == 208u, "region 1 = banda 4 planos");

	// Region invalida (bottom <= top) se rechaza.
	const eng::scene::WorldRegion bad {208u, 208u, eng::scene::LayerPlayfield::Any,
					   eng::graphics::composition::SceneMode::Standard, 0u};
	check(!w.add_region(bad), "region invalida rechazada");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Layer declarativa (scroll/prefer) + WorldRegion validado.\n");
	return 0;
}
