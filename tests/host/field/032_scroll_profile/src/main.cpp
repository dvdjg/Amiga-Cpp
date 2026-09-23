// ============================================================================
// Test HOST-032: perfil de scroll estático (SpeedPolicy)
// ============================================================================
//
// Valida `eng::field::ScrollProfile` y sus alias (`ScrollProgressive`,
// `ScrollFast1/2/4`): relleno por frame, guarda de lookahead, paso máximo por
// tamaño de tile e invariante de guarda (compile-time). Sin hardware.

#include <cstdio>

#include <eng/field/scroll_profile.hpp>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) { std::printf("[FAIL] %s\n", what); ++g_fail; }
}
using eng::field::ScrollProgressive;
using eng::field::ScrollFast1;
using eng::field::ScrollFast2;
using eng::field::ScrollFast4;
using eng::field::ScrollProfile;
using eng::field::ProgressiveFill;
using eng::field::TileBurstFill;
using eng::field::StripPrerenderFill;
using eng::field::GuardTiles;
} // namespace

int main() {
	// El perfil por defecto NO impone paso ni guarda (comportamiento clásico).
	check(ScrollProgressive::fill_tiles == 0 && ScrollProgressive::prefill == false,
	      "progresivo: sin relleno por ráfagas");
	check(ScrollProgressive::max_step_px(16) == 0 && ScrollProgressive::guard_px(16) == 0,
	      "progresivo: sin override de paso/guarda");

	// Perfiles rápidos: paso = N tiles, guarda = (N+1) tiles de lookahead.
	check(ScrollFast1::fill_tiles == 1 && ScrollFast1::max_step_px(16) == 16,
	      "fast1: 1 tile/frame");
	check(ScrollFast2::fill_tiles == 2 && ScrollFast2::max_step_px(16) == 32,
	      "fast2: 2 tiles/frame");
	check(ScrollFast4::fill_tiles == 4 && ScrollFast4::max_step_px(16) == 64,
	      "fast4: 4 tiles/frame");
	check(ScrollFast1::guard_px(16) == 32 && ScrollFast2::guard_px(16) == 48 &&
	      ScrollFast4::guard_px(16) == 80,
	      "guardas fast = (N+1) tiles");
	check(ScrollFast1::prefill && ScrollFast2::prefill && ScrollFast4::prefill,
	      "perfiles rápidos pre-rellenan");

	// Escala con el tamaño de tile (la política es independiente del tile).
	check(ScrollFast2::max_step_px(32) == 64 && ScrollFast2::guard_px(32) == 96,
	      "escala con tile=32");

	// Perfil a medida con dirección laceda a frontera de tile.
	using Latched = ScrollProfile<TileBurstFill<3>, GuardTiles<4>, true>;
	check(Latched::fill_tiles == 3 && Latched::guard_tiles == 4 && Latched::direction_latched,
	      "perfil a medida (3 tiles, guarda 4, latched)");

	// StripPrerender se comporta como un burst de C columnas.
	using Strip = ScrollProfile<StripPrerenderFill<6>, GuardTiles<7>>;
	check(Strip::fill_tiles == 6 && Strip::max_step_px(16) == 96, "strip prerender 6");

	if (g_fail != 0) { std::printf("%d fallo(s)\n", g_fail); return 1; }
	std::printf("OK: ScrollProfile (paso, guarda, prefill, invariante) validado.\n");
	return 0;
}
