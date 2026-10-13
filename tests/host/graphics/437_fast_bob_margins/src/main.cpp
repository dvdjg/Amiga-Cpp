// ============================================================================
// Test HOST-437: márgenes del FastCopy (`graphics/fast_bob_margins.hpp`).
// ============================================================================
//
// Respalda el cálculo de F5 (`ROADMAP_SPRITES_COMPLETO.md` §3.4): el asset declara el
// desplazamiento máximo por eje y este helper da el padding y el tamaño de hoja que el
// pipeline cocina para el camino `BobDraw::Opaque` + `FastBobLayer` (dual playfield fast
// BOBs, HOST-355). Con doble buffer los márgenes se doblan.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/437_fast_bob_margins

#include <cstdio>

#include <eng/graphics/fast_bob_margins.hpp>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

} // namespace

int main() {
	std::printf("Test HOST-437 fast_bob_margins (F5)\n");
	std::printf("===================================\n");

	using eng::graphics::fast_bob_margins;

	// Sin movimiento: sin márgenes.
	{
		const auto m = fast_bob_margins(16u, 16u, 0u, 0u, 0u, 0u, false);
		CHECK(m.left == 0u && m.right == 0u && m.top == 0u && m.bottom == 0u);
		CHECK(m.padded_w == 16u && m.padded_h == 16u);
	}
	// Paso 1 px/frame en las cuatro direcciones: un píxel por lado.
	{
		const auto m = fast_bob_margins(16u, 8u, 1u, 1u, 1u, 1u, false);
		CHECK(m.left == 1u && m.right == 1u && m.top == 1u && m.bottom == 1u);
		CHECK(m.padded_w == 18u && m.padded_h == 10u);
	}
	// Velocidades distintas por eje y dirección.
	{
		const auto m = fast_bob_margins(24u, 12u, 3u, 5u, 2u, 0u, false);
		CHECK(m.left == 3u && m.right == 5u && m.top == 2u && m.bottom == 0u);
		CHECK(m.padded_w == 32u && m.padded_h == 14u);
	}
	// Doble buffer: los cuatro se doblan.
	{
		const auto m = fast_bob_margins(24u, 12u, 3u, 5u, 2u, 1u, true);
		CHECK(m.left == 6u && m.right == 10u && m.top == 4u && m.bottom == 2u);
		CHECK(m.padded_w == 40u && m.padded_h == 18u);
	}
	// `constexpr` de compilación: el asset lo usa en tablas estáticas.
	{
		constexpr auto m = fast_bob_margins(16u, 16u, 2u, 2u, 2u, 2u, true);
		static_assert(m.padded_w == 24u && m.padded_h == 24u);
		static_assert(m.left == 4u && m.bottom == 4u);
	}

	if (g_failures == 0) {
		std::printf("OK: margenes del FastCopy validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
