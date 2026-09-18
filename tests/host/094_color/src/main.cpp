// ============================================================================
// Test HOST-094: color RGB444 (eng::util::color).
// ============================================================================
//
// Respalda `eng/core/util/color.hpp`: empaquetado, interpolación, escalado y HSV.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/094_color

#include <cstdio>

#include <eng/core/util/color.hpp>

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
	std::printf("== HOST-094 color ==\n");

	// --- Empaquetado ---------------------------------------------------------
	{
		const eng::u16 c = eu::rgb444(1u, 3u, 8u);
		check(c == 0x138u, "rgb444(1,3,8) = 0x138");
		check(eu::rgb444_r(c) == 1u && eu::rgb444_g(c) == 3u && eu::rgb444_b(c) == 8u,
		      "extracción de canales");
	}

	// --- Interpolación -------------------------------------------------------
	{
		check(eu::lerp444(0x000u, 0xfffu, 0u, 1u) == 0x000u, "lerp num=0 = a");
		check(eu::lerp444(0x000u, 0xfffu, 1u, 1u) == 0xfffu, "lerp num=den = b");
		check(eu::lerp444(0x000u, 0xfffu, 1u, 2u) == 0x777u, "lerp a mitad = 0x777");
		check(eu::lerp444(0x123u, 0x456u, 3u, 0u) == 0x123u, "lerp den=0 = a");
	}

	// --- Escalado ------------------------------------------------------------
	{
		check(eu::scale444(0x246u, 1u, 2u) == 0x123u, "scale 1/2");
		check(eu::scale444(0x888u, 2u, 1u) == 0xfffu, "scale satura a 15");
	}

	// --- HSV -----------------------------------------------------------------
	{
		check(eu::hsv_to_rgb444(0u, 255u, 255u) == 0xf00u, "HSV rojo");
		check(eu::hsv_to_rgb444(512u, 255u, 255u) == 0x0f0u, "HSV verde");
		check(eu::hsv_to_rgb444(1024u, 255u, 255u) == 0x00fu, "HSV azul");
		check(eu::hsv_to_rgb444(0u, 0u, 255u) == 0xfffu, "HSV blanco (sat 0)");
		check(eu::hsv_to_rgb444(0u, 0u, 0u) == 0x000u, "HSV negro (val 0)");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: color validado.\n");
	return 0;
}
