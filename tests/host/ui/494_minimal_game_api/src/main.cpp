// ============================================================================
// Test HOST-494: contrato de compilacion del juego minimo de alto nivel.
// ============================================================================
//
// El cuerpo del juego debe seguir siendo ~30 lineas de fachada: Screen + input +
// present, sin FramePlan, planos, memoria, backend ni registros. El backend de este
// test solo existe para instanciar el tipo App; no se construye ni se ejecuta.
//
//   bash tools/run-host-tests.sh tests/host/api/494_minimal_game_api

#include <cstdio>

#include <eng/api/api.hpp>

namespace {

struct MockBackend {
	void boot() {}
	void wait_vblank() {}
};

struct MiniGame {
	eng::s16 x = 144;
	eng::s16 y = 112;

	void init(auto&) {}

	void update(auto& app) {
		auto& in = app.input();
		if (in.pad0.left) x = static_cast<eng::s16>(x - 2);
		if (in.pad0.right) x = static_cast<eng::s16>(x + 2);
		if (in.pad0.up) y = static_cast<eng::s16>(y - 2);
		if (in.pad0.down) y = static_cast<eng::s16>(y + 2);
	}

	void render(auto& app) {
		auto screen = app.screen();
		screen.clear(0u);
		(void)screen.fill_box(eng::Box {x, y, 16u, 16u}, 1u);
		app.present();
	}
};

using MiniApp = eng::App<MockBackend, MiniGame>;
static_assert(requires(MiniApp& app, MiniGame& game) {
	game.init(app);
	game.update(app);
	game.render(app);
});

} // namespace

int main() {
	std::printf("OK: juego minimo (Screen + input + present) compila por la fachada.\n");
	return 0;
}
