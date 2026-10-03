// ============================================================================
// Test HOST-413: escalera de motores por geometría runtime (paso 4 de §7)
// ============================================================================
//
// Verifica `eng::playfield::ScrollLadder`: registra motores (`ScrollLayer<Backend>`) con su geometría
// (`RuntimeScrollGeometry`) y `pick` elige el que coincide con una geometría cargada en runtime.
// Usa motores mock (la escalera es genérica sobre el motor).
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/field/413_scroll_ladder

#include <cstdio>

#include <eng/field/scroll_ladder.hpp>
#include <eng/field/scroll_layer.hpp>

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

struct MockBackend {};

/// Motor mock: solo identifica que fue elegido.
struct MockEngine : eng::playfield::ScrollLayer<MockBackend> {
	int id = 0;
	bool begin(eng::MemoryManager&, MockBackend&) noexcept override { return true; }
	void frame(MockBackend&) noexcept override {}
};

void test_pick() {
	MockEngine a {};
	MockEngine b {};
	a.id = 1;
	b.id = 2;
	eng::playfield::ScrollLadder<MockBackend, 2u> ladder {};
	const auto ga = eng::playfield::runtime_scroll_geometry(320u, 208u, 3u, 16u, 16u, 2u, 1u, false,
								0u, 0u, 0u);
	const auto gb = eng::playfield::runtime_scroll_geometry(320u, 256u, 3u, 16u, 16u, 2u, 1u, false,
								0u, 0u, 0u);
	check(ga.has_value() && gb.has_value(), "geometrías construidas");
	check(ladder.add(a, *ga) && ladder.add(b, *gb), "registra 2 motores");
	check(ladder.count() == 2u, "2 motores");
	check(ladder.pick(*ga).get() == static_cast<eng::playfield::ScrollLayer<MockBackend>*>(&a),
	      "pick 208 → motor A");
	check(ladder.pick(*gb).get() == static_cast<eng::playfield::ScrollLayer<MockBackend>*>(&b),
	      "pick 256 → motor B");

	// Geometría no registrada → inválido.
	const auto gc = eng::playfield::runtime_scroll_geometry(320u, 192u, 3u, 16u, 16u, 2u, 1u, false,
								0u, 0u, 0u);
	check(!ladder.pick(*gc).valid(), "geometría no registrada → Ref inválido");
}

} // namespace

int main() {
	test_pick();
	if (failures == 0) {
		std::printf("OK: escalera de motores por geometría runtime validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
