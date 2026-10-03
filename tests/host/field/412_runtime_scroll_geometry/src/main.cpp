// ============================================================================
// Test HOST-412: geometría del anillo en RUNTIME == NTTP (paso 4 de §7)
// ============================================================================
//
// Verifica que `runtime_scroll_geometry(...)` calcula EXACTAMENTE los mismos campos que la
// `StripScrollGeometry<...>` compilada (NTTP) para varias geometrías, y que rechaza las
// invariantes inválidas. Es la referencia de equivalencia (el motor rápido sigue siendo NTTP).
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/field/412_runtime_scroll_geometry

#include <cstdio>

#include <eng/field/runtime_scroll_geometry.hpp>
#include <eng/field/strip_scroller.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

using eng::playfield::RuntimeScrollGeometry;
using eng::playfield::ScrollGeomError;

/// Compara todos los campos de la geometría runtime con los del NTTP `Geom`.
template <class Geom>
void check_all(const RuntimeScrollGeometry& g, const char* what) {
	const bool ok = g.viewport_w == Geom::viewport_w && g.viewport_h == Geom::viewport_h &&
			g.planes == Geom::planes && g.tile_w == Geom::tile_w && g.tile_h == Geom::tile_h &&
			g.split_vertical == Geom::split_vertical && g.split_line == Geom::split_line &&
			g.split_crosses_255 == Geom::split_crosses_255 &&
			g.visible_words == Geom::visible_words && g.ring_w_words == Geom::ring_w_words &&
			g.ring_w_bytes == Geom::ring_w_bytes && g.fetch_words == Geom::fetch_words &&
			g.bpl_mod == Geom::bpl_mod && g.ring_h == Geom::ring_h &&
			g.column_tiles == Geom::column_tiles && g.row_tiles == Geom::row_tiles &&
			g.column_planelines == Geom::column_planelines &&
			g.column_blits == Geom::column_blits && g.strip_row_words == Geom::strip_row_words &&
			g.row_planelines == Geom::row_planelines && g.bltdmod_row == Geom::bltdmod_row &&
			g.strip_words == Geom::strip_words && g.bltdmod_col == Geom::bltdmod_col;
	check(ok, what);
}

template <eng::u16 VW, eng::u16 VH, eng::u8 P, eng::u16 TW, eng::u16 TH, eng::u16 G, eng::u16 F,
	  bool S, eng::u16 RW, eng::u16 RL, eng::u16 MW>
void compare(const char* what) {
	using Geom = eng::playfield::StripScrollGeometry<VW, VH, P, TW, TH, G, F, S, RW, RL, MW>;
	const auto r = eng::playfield::runtime_scroll_geometry(VW, VH, P, TW, TH, G, F, S, RW, RL, MW);
	check(r.has_value(), what);
	if (r.has_value()) {
		check_all<Geom>(*r, what);
	}
}

void test_equivalence() {
	compare<320u, 208u, 5u, 16u, 16u, 2u, 1u, false, 0u, 0u, 0u>("320x208 5p (202)");
	compare<320u, 256u, 3u, 16u, 16u, 2u, 1u, false, 0u, 448u, 40u>("320x256 3p (204)");
	compare<320u, 208u, 3u, 16u, 16u, 2u, 1u, false, 0u, 320u, 20u>("320x208 3p anillo 320 (205)");
	compare<320u, 128u, 3u, 32u, 32u, 2u, 1u, false, 0u, 0u, 10u>("320x128 32px tiles");
	compare<256u, 256u, 4u, 16u, 16u, 2u, 1u, true, 0u, 0u, 0u>("256x256 4p split");
}

void test_errors() {
	check(!eng::playfield::runtime_scroll_geometry(320u, 208u, 3u, 24u, 16u).has_value(),
	      "tile 24 → error");
	check(eng::playfield::runtime_scroll_geometry(320u, 208u, 3u, 24u, 16u).error() ==
		      ScrollGeomError::BadTile,
	      "tile 24 → BadTile");
	check(eng::playfield::runtime_scroll_geometry(300u, 208u, 3u, 16u, 16u).error() ==
		      ScrollGeomError::BadViewport,
	      "viewport no múltiplo → BadViewport");
	check(eng::playfield::runtime_scroll_geometry(320u, 208u, 3u, 16u, 16u, 1u).error() ==
		      ScrollGeomError::BadGuard,
	      "guarda < 2 → BadGuard");
}

} // namespace

int main() {
	test_equivalence();
	test_errors();
	if (failures == 0) {
		std::printf("OK: geometría runtime == NTTP + invariantes validadas.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
