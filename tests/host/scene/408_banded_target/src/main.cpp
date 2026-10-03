// ============================================================================
// Test HOST-408: dibujo split-aware — reparto por banda (etapa 4 §7)
// ============================================================================
//
// Verifica `eng::scene::for_each_band_part`: un rect de pantalla se reparte entre las bandas, con
// cada trozo recortado a su banda. Puro, sin motores.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/scene/408_banded_target

#include <cstdio>

#include <eng/scene/banded_target.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

using eng::scene::BandSpan;
using eng::scene::for_each_band_part;
using eng::scene::LayerRole;

BandSpan g_bands[2] = {{0u, 128u, LayerRole::Foreground}, {128u, 128u, LayerRole::Foreground}};

void test_within_band() {
	eng::u16 count = 0u;
	eng::u16 idx = 99u;
	eng::Box got {};
	for_each_band_part(eng::Box {10, 20, 30, 40}, eng::Span<const BandSpan> {g_bands, 2u},
			   [&](eng::u16 i, const eng::Box& b) {
				   ++count;
				   idx = i;
				   got = b;
			   });
	check(count == 1u && idx == 0u, "rect dentro de la banda 0 → 1 llamada");
	check(got.x == 10 && got.y == 20 && got.h == 40u, "trozo = rect íntegro");
}

void test_crossing_split() {
	eng::u16 count = 0u;
	eng::u16 idx[2] = {0u, 0u};
	eng::Box part[2] {};
	for_each_band_part(eng::Box {10, 100, 20, 60}, eng::Span<const BandSpan> {g_bands, 2u},
			   [&](eng::u16 i, const eng::Box& b) {
				   idx[count] = i;
				   part[count] = b;
				   ++count;
			   });
	check(count == 2u, "rect cruzando la línea → 2 llamadas");
	if (count == 2u) {
		check(idx[0] == 0u && part[0].y == 100 && part[0].h == 28u, "trozo superior (100..127)");
		check(idx[1] == 1u && part[1].y == 128 && part[1].h == 32u, "trozo inferior (128..159)");
	}
}

void test_no_overlap() {
	eng::u16 count = 0u;
	for_each_band_part(eng::Box {0, 300, 10, 10}, eng::Span<const BandSpan> {g_bands, 2u},
			   [&](eng::u16, const eng::Box&) { ++count; });
	check(count == 0u, "rect fuera de todas las bandas → 0 llamadas");
}

} // namespace

int main() {
	test_within_band();
	test_crossing_split();
	test_no_overlap();
	if (failures == 0) {
		std::printf("OK: reparto por banda (etapa 4 §7) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
