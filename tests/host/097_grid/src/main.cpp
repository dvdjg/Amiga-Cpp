// ============================================================================
// Test HOST-097: rejilla, isométrico y hex (eng::util::grid).
// ============================================================================
//
// Respalda `eng/core/util/grid.hpp`: tile<->pixel, proyección diamante e inversa y
// vecinos/distancia hexagonal.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/097_grid

#include <cstdio>

#include <eng/core/util/grid.hpp>

namespace eu = eng::util;

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
	std::printf("== HOST-097 grid ==\n");

	// --- tile <-> pixel ------------------------------------------------------
	{
		const eng::Point2s px = eu::grid_to_world(eu::TileCoord {3, 2}, 16u, 16u);
		check(px.x == 48 && px.y == 32, "grid_to_world (3,2)*16 = (48,32)");
		check(eu::world_to_grid<16, 16>(48, 32) == eu::TileCoord {3, 2}, "world_to_grid");
		check(eu::world_to_grid<16, 16>(-1, -1) == eu::TileCoord {-1, -1},
		      "world_to_grid floor con negativos (-1)");
		check(eu::world_to_grid<16, 16>(-17, 0) == eu::TileCoord {-2, 0},
		      "world_to_grid floor (-17 -> -2)");
		check(eu::world_to_grid<16, 16>(15, 16) == eu::TileCoord {0, 1},
		      "world_to_grid borde");
	}

	// --- isométrico ----------------------------------------------------------
	{
		const eng::Point2s s00 = eu::iso_to_screen(eu::TileCoord {0, 0}, 8u, 4u);
		check(s00.x == 0 && s00.y == 0, "iso (0,0)");
		const eng::Point2s s21 = eu::iso_to_screen(eu::TileCoord {2, 1}, 8u, 4u);
		check(s21.x == 8 && s21.y == 12, "iso (2,1) = (8,12)");
		check(eu::screen_to_iso<8, 4>(s21.x, s21.y) == eu::TileCoord {2, 1},
		      "inversa iso (2,1)");
		check(eu::screen_to_iso<8, 4>(0, 0) == eu::TileCoord {0, 0}, "inversa iso (0,0)");
	}

	// --- hex -----------------------------------------------------------------
	{
		eu::Hex vecinos[6];
		eu::hex_neighbors(eu::Hex {0, 0}, vecinos);
		check(vecinos[0] == eu::Hex {1, 0} && vecinos[1] == eu::Hex {1, -1} &&
			      vecinos[2] == eu::Hex {0, -1} && vecinos[3] == eu::Hex {-1, 0} &&
			      vecinos[4] == eu::Hex {-1, 1} && vecinos[5] == eu::Hex {0, 1},
		      "hex_neighbors (6 direcciones)");

		check(eu::hex_neighbor(eu::Hex {2, 1}, 0u) == eu::Hex {3, 1}, "hex vecino E");
		check(eu::hex_neighbor(eu::Hex {2, 1}, 3u) == eu::Hex {1, 1}, "hex vecino W");

		check(eu::hex_distance(eu::Hex {0, 0}, eu::Hex {0, 0}) == 0, "hex distancia 0");
		check(eu::hex_distance(eu::Hex {0, 0}, eu::Hex {1, -1}) == 1, "hex distancia 1");
		check(eu::hex_distance(eu::Hex {0, 0}, eu::Hex {1, 1}) == 2, "hex distancia 2");
		check(eu::hex_distance(eu::Hex {2, -1}, eu::Hex {0, 0}) == 2, "hex distancia 2 (inversa)");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: grid validado.\n");
	return 0;
}
