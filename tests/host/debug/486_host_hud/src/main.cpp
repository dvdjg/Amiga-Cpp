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
	int text_calls = 0;
	char name[64] {};
	char state[64] {};
	char stats[64] {};

	void clear() noexcept { ++clears; }

	void text(int x, int y, const char* text, eng::u32 rgb) noexcept {
		(void)rgb;
		if (text == nullptr || x != eng::debug::HostHud::kMarginX ||
		    y != eng::debug::HostHud::kMarginY) {
			return;
		}
		++text_calls;
		char block[256] {};
		std::strncpy(block, text, sizeof(block) - 1u);
		char* lines[3] = {name, state, stats};
		char* p = block;
		for (unsigned i = 0u; i < 3u && *p != '\0'; ++i) {
			char* end = p;
			while (*end != '\0' && *end != '\n') {
				++end;
			}
			if (*end == '\n') {
				*end++ = '\0';
			}
			std::strncpy(lines[i], p, 63u);
			lines[i][63] = '\0';
			p = end;
		}
	}
};

void test_identity_and_fps() {
	FakeOverlay overlay {};
	eng::debug::HostHud hud {};
	hud.draw(overlay, "486 TEST HUD", eng::debug::Version {1u, 2u}, 0u, 1000u);
	CHECK(overlay.text_calls == 1); // un solo bloque multil?nea
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
