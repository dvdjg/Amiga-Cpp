// ============================================================================
// Test HOST-486: HUD de depuración del host (`eng/debug/host_hud.hpp`).
// ============================================================================
//
// Verifica el contrato que comparten todas las demos/juegos:
//   - una línea con nombre + versión (vM.m.B);
//   - una línea con frame renderizado + fps ×10;
//   - una línea opcional de telemetría propia;
//   - `clear` opcional para respetar otra telemetría del overlay.
//
// El overlay es un doble de prueba: no toca WinUAE ni consume bitplanes/DMA.

#include <cstdio>
#include <cstring>

#include <eng/debug/host_hud.hpp>
#include <eng/debug/prof.hpp>

namespace {

int g_fail = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_fail;                                                      \
		}                                                                 \
	} while (0)

struct FakeOverlay {
	int clears = 0;
	char name[64] {};
	char state[64] {};
	char stats[64] {};

	void clear() noexcept { ++clears; }

	void text(int x, int y, const char* text, eng::u32 rgb) noexcept {
		(void)x;
		(void)rgb;
		char* dst = y == 4 ? name : y == 12 ? state : y == 20 ? stats : nullptr;
		if (dst == nullptr || text == nullptr) {
			return;
		}
		std::strncpy(dst, text, 63u);
		dst[63] = '\0';
	}
};

void test_identity_and_fps() {
	FakeOverlay overlay {};
	eng::debug::HostHud hud {};
	hud.draw(overlay, "486 TEST HUD", eng::debug::Version {1u, 2u}, 0u, 1000u);
	CHECK(std::strncmp(overlay.name, "486 TEST HUD v1.2.", 18u) == 0);
	CHECK(std::strncmp(overlay.state, "F0 0.0FPS", 9u) == 0);
	CHECK(overlay.clears == 1);

	// 20 frames en una ventana de 1 s: fps10 = 200.
	hud.draw(overlay, "486 TEST HUD", eng::debug::Version {1u, 2u}, 20u,
		 1000u + eng::debug::prof_clock_hz);
	CHECK(std::strcmp(overlay.state, "F20 20.0FPS") == 0);
	CHECK(hud.fps10() == 200u);
	CHECK(overlay.clears == 2);
}

void test_stats_and_clear() {
	FakeOverlay overlay {};
	eng::debug::HostHud hud {};
	hud.draw(overlay, "486 TEST HUD", eng::debug::Version {1u, 0u}, 7u, 1000u,
		 "LOAD 12KC PIECES 91");
	CHECK(std::strcmp(overlay.stats, "LOAD 12KC PIECES 91") == 0);

	overlay.clears = 0;
	hud.draw(overlay, "486 TEST HUD", eng::debug::Version {1u, 0u}, 8u, 1001u,
		 nullptr, false);
	CHECK(overlay.clears == 0); // respeta la telemetría previa del overlay
}

} // namespace

int main() {
	test_identity_and_fps();
	test_stats_and_clear();
	if (g_fail == 0) {
		std::printf("HOST-486 host_hud: OK\n");
		return 0;
	}
	std::printf("HOST-486 host_hud: %d fallos\n", g_fail);
	return 1;
}
