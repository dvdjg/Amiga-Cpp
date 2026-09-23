// ============================================================================
// Test HOST-094: color RGB444 (eng::util::color).
// ============================================================================
//
// Respalda `eng/core/util/color.hpp`: empaquetado, interpolación, escalado, HSV y
// operaciones de paleta completa (`palette_lerp`/`palette_scale`).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/094_color

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

	// --- Paleta: transición (palette_lerp) ----------------------------------
	{
		const eng::u16 a[4] = {0x000u, 0x111u, 0x222u, 0x333u};
		const eng::u16 b[4] = {0xfffu, 0xeeeu, 0xdddu, 0xcccu};
		eng::u16 dst[4] = {};
		const eng::usize n = eu::palette_lerp(dst,
						      a,
						      b, 1u, 2u);
		check(n == 4u, "palette_lerp escribe 4");
		bool ok = true;
		for (eng::usize i = 0; i < 4u; ++i) {
			if (dst[i] != eu::lerp444(a[i], b[i], 1u, 2u)) {
				ok = false;
			}
		}
		check(ok, "palette_lerp = lerp444 por color");
		eu::palette_lerp(dst, a,
				 b, 1u, 1u);
		check(dst[0] == b[0] && dst[3] == b[3], "palette_lerp num=den = b");
		eng::u16 small[2] = {};
		check(eu::palette_lerp(small, a,
				       b, 1u, 2u) == 2u,
		      "palette_lerp recorta al mínimo");
	}

	// --- Paleta: fundido (palette_scale) ------------------------------------
	{
		const eng::u16 src[3] = {0xfffu, 0x888u, 0x800u};
		eng::u16 dst[3] = {};
		eu::palette_scale(dst, src, 0u, 1u);
		check(dst[0] == 0u && dst[1] == 0u && dst[2] == 0u, "palette_scale num=0 = negro");
		eu::palette_scale(dst, src, 1u, 1u);
		check(dst[0] == src[0] && dst[2] == src[2], "palette_scale num=den = igual");
		eng::u16 buf[1] = {0xfffu};
		eu::palette_scale(buf, buf, 1u, 2u);
		check(buf[0] == eu::scale444(0xfffu, 1u, 2u), "palette_scale in place");
	}

	// --- Degradado multi-parada (gradient444) -------------------------------
	{
		const eng::u16 two[2] = {0x000u, 0xfffu};
		check(eu::gradient444(two, 0u, 8u) == 0x000u,
		      "gradient [n,r] en 0 = primer color");
		check(eu::gradient444(two, 8u, 8u) == 0xfffu,
		      "gradient [n,r] en den = ultimo color");
		check(eu::gradient444(two, 4u, 8u) == 0x777u,
		      "gradient [n,r] a mitad = 0x777");

		const eng::u16 three[3] = {0x000u, 0xf00u, 0xfffu};
		check(eu::gradient444(three, 4u, 8u) == 0xf00u,
		      "gradient 3 paradas: en 1/2 = parada central");
		check(eu::gradient444(three, 6u, 8u) ==
			      eu::lerp444(0xf00u, 0xfffu, 4u, 8u),
		      "gradient 3 paradas: tramo 2 interpolado");
		check(eu::gradient444(three, 6u, 8u) != 0xf00u &&
			      eu::gradient444(three, 6u, 8u) != 0xfffu,
		      "gradient 3 paradas: 3/4 no es una parada");

		const eng::u16 one[1] = {0x246u};
		check(eu::gradient444(one, 3u, 8u) == 0x246u,
		      "gradient de una parada = esa parada");
		check(eu::gradient444(eng::Span<const eng::u16> {}, 3u, 8u) == 0u,
		      "gradient vacio = 0");
		check(eu::gradient444(two, 3u, 0u) == 0x000u,
		      "gradient den=0 = primer color");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: color validado.\n");
	return 0;
}
