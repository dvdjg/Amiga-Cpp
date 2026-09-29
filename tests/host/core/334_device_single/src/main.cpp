// ============================================================================
// Test HOST-334: ergonomia de un solo elemento en Device (sin envolver Span a mano).
// ============================================================================
//
// Respalda el overload `Device::blitter_or_bobs(const OrBob&, ...)` (azúcar del de `Span`):
// con un backend falso comprueba que un solo `OrBob` se reenvía como un lote de 1 entrada,
// evitando escribir `Span<const OrBob>{&bob, 1u}` en el llamador.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/334_device_single

#include <cstdio>

#include <eng/api/device.hpp>
#include <eng/graphics/blitter_state.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Backend falso: implementa solo la operación que `Device` reenvía.
struct FakeBackend {
	int calls = 0;
	eng::u32 last_count = 0;
	bool blitter_or_bobs(const eng::graphics::OrBob*, eng::u32 count, eng::u16, eng::u16,
			     eng::s16, eng::s16) noexcept {
		++calls;
		last_count = count;
		return true;
	}
};

} // namespace

int main() {
	std::printf("== HOST-334 device_single ==\n");

	FakeBackend backend {};
	eng::Device<FakeBackend> device {backend, {}};

	const eng::graphics::OrBob bob {};
	check(device.blitter_or_bobs(bob, 4u, 8u, -2, 0),
	      "overload de un solo BOB reenvia");
	check(backend.calls == 1 && backend.last_count == 1u, "lote de 1 entrada");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Device::blitter_or_bobs(const OrBob&) (un elemento) validado.\n");
	return 0;
}
