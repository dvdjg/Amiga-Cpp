// ============================================================================
// Test HOST-396: ruta de scroll continua (eng/field/scroll_route.hpp).
// ============================================================================
//
// Invariantes de `playfield::ScrollRoute`: cada frame mueve como mucho 1 px por eje (los motores
// con `max_step` pequeno, p. ej. el corcoscru, rechazan saltos) y **al menos un eje cambia** (cada
// frame es una imagen distinta). La Y se mantiene en `[0, YMax]`.
//
//   bash tools/run-host-tests.sh tests/host/field/320_scroll_route

#include <cstdio>

#include <eng/field/scroll_route.hpp>

namespace {

int g_fail = 0;
using Route = eng::playfield::ScrollRoute<64u>;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-396 scroll_route ==\n");
	Route r {};
	eng::s32 px = r.x;
	eng::s32 py = r.y;
	bool within_step = true;
	bool always_moves = true;
	bool y_in_range = true;
	eng::s32 min_y = r.y;
	eng::s32 max_y = r.y;
	// 3 ciclos completos de las 5 fases (256 frames cada una).
	for (eng::u32 f = 0u; f < 5u * 256u * 3u; ++f) {
		r.advance(f);
		const eng::s32 dx = r.x - px;
		const eng::s32 dy = r.y - py;
		if (dx < -1 || dx > 1 || dy < -1 || dy > 1) within_step = false;
		if (dx == 0 && dy == 0) always_moves = false;
		if (r.y < 0 || r.y > 64) y_in_range = false;
		if (r.y < min_y) min_y = r.y;
		if (r.y > max_y) max_y = r.y;
		px = r.x;
		py = r.y;
	}
	check(within_step, "cada frame mueve <= 1 px por eje (sin saltos)");
	check(always_moves, "cada frame cambia al menos un eje (imagen distinta)");
	check(y_in_range, "la Y se mantiene en [0, 64]");
	check(min_y == 0 && max_y == 64, "la Y recorre todo el rango");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: ruta de scroll continua (<= 1 px/eje, sin repetir frame) validada.\n");
	return 0;
}
