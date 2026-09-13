// ============================================================================
// Test HOST-033: avance en fronteras de tile (prefill/latch) y staging en Y
// ============================================================================
//
// Valida `eng::field::snap_to_tiles` y los derivados del perfil que usan los
// perfiles rápidos: avance por tiles completos (dirección laceda a frontera) y
// staging vertical del corkscrew (`y_staging_tiles`).

#include <cstdio>

#include <eng/field/scroll_profile.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}
using eng::s32;
using eng::field::ScrollProgressive;
using eng::field::ScrollFast1;
using eng::field::ScrollFast2;
using eng::field::ScrollFast4;
using eng::field::snap_to_tiles;
} // namespace

int main() {
	// Avance por tiles completos: redondeo hacia cero, signo preservado.
	check(snap_to_tiles(32, 16) == 32, "32 -> 32");
	check(snap_to_tiles(17, 16) == 16, "17 -> 16");
	check(snap_to_tiles(-17, 16) == -16, "-17 -> -16");
	check(snap_to_tiles(5, 16) == 0, "5 -> 0 (no llega a un tile)");
	check(snap_to_tiles(0, 16) == 0 && snap_to_tiles(9, 0) == 9, "0 y tile=0");
	check(snap_to_tiles(255, 64) == 192 && snap_to_tiles(-255, 64) == -192, "tile=64");

	// Staging vertical: el clásico usa 2 bloques; el rápido, la guarda (>=2).
	check(ScrollProgressive::y_staging_tiles() == 2, "progresivo: staging 2");
	check(ScrollFast1::y_staging_tiles() == 2, "fast1: staging 2");
	check(ScrollFast2::y_staging_tiles() == 3, "fast2: staging 3");
	check(ScrollFast4::y_staging_tiles() == 5, "fast4: staging 5");

	// Los perfiles rápidos llevan prefill y dirección laceda; el clásico no.
	check(!ScrollProgressive::prefill && !ScrollProgressive::direction_latched,
	      "progresivo: ni prefill ni latch");
	check(ScrollFast2::prefill && ScrollFast2::direction_latched, "fast2: prefill+latch");
	// Con prefill, el paso es múltiplo de tile: la dirección solo cambia en frontera.
	check(ScrollFast2::max_step_px(16) % 16 == 0 && ScrollFast4::max_step_px(16) % 16 == 0,
	      "paso rápido alineado a tile");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: snap a frontera de tile, latch y staging Y validados.\n");
	return 0;
}
