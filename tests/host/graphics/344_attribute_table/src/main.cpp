// ============================================================================
// Test HOST-344: attribute table (tilemap::AttributeTable) - F7.2 (set_attribute).
// ============================================================================
//
// Respalda `eng/graphics/tilemap/attribute_table.hpp`: rejilla mutable de paleta por bloque
// (16x16 px), `set`/`get`/`fill` con limites. Da el `set_attribute` de una capa de tiles sin
// tocar el framebuffer.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/344_attribute_table

#include <cstdio>

#include <eng/graphics/tilemap/attribute_table.hpp>

namespace tm = eng::graphics::tilemap;

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
	std::printf("== HOST-344 attribute_table ==\n");

	eng::u8 cells[4u * 3u] {};
	tm::AttributeTable at {eng::Span<eng::u8> {cells, 4u * 3u}, 4u, 3u};
	check(at.valid(), "tabla valida");
	check(at.get(0u, 0u) == 0u, "inicial 0");

	check(at.set(2u, 1u, 3u), "set ok");
	check(at.get(2u, 1u) == 3u, "get refleja el set");
	check(!at.set(9u, 9u, 1u), "fuera de rango -> false");
	check(at.get(9u, 9u) == 0u, "get fuera -> 0");

	at.fill(2u);
	check(at.get(0u, 0u) == 2u && at.get(3u, 2u) == 2u, "fill cubre toda la tabla");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: AttributeTable (set/get/fill) validado.\n");
	return 0;
}
