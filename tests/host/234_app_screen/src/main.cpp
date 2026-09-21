// ============================================================================
// Test HOST-234: fachada de juego `eng::App` + `eng::Screen` (eng/api/game.hpp).
// ============================================================================
//
// Valida el borrador del API publico de juego sobre lo que ya existe: `App` junta el bucle,
// la pantalla y las tareas, y el juego se escribe con `init/update/render(App&)` sin ver el
// backend ni `GameContext`/`FramePlan`. `Screen` es el contexto de dibujo de alto nivel.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/234_app_screen

#include <cstdio>

#include <eng/api/game.hpp>

using namespace eng;

namespace {

alignas(16) u8 g_chip[512 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = LinearArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

/// Backend minimo: solo el ciclo que el bucle necesita (`boot` + `wait_vblank`). Sin
/// `execute_frame_plan`: `App::present` lo omite.
struct MockBackend {
	void boot() {}
	void wait_vblank() {}
	template <class F, class P>
	void wait_vblank(F, P) {} // variante con bombeo de fondo (no usada aqui)
};

/// Juego de prueba con el contrato del API publico (`auto&` = no nombra el tipo del App).
struct TestGame {
	graphics::composition::Scene* scene = nullptr;
	bool inited = false;
	bool updated = false;
	bool rendered = false;
	u32 seen_frame = 0xffffffffu;

	void init(auto& app) {
		inited = true;
		app.bind_scene(*scene);
	}
	void update(auto& app) {
		updated = true;
		seen_frame = app.frame();
		Screen s = app.screen();
		s.clear(0);
		(void)s.fill(Box {0, 0, 4u, 4u}, 1u);
		app.present();
	}
	void render(auto& app) {
		rendered = true;
		(void)app;
	}
};

u32 bits_in_plane(const u8* base, u32 bytes) {
	u32 n = 0;
	for (u32 i = 0; i < bytes; ++i) {
		u8 b = base[i];
		while (b != 0u) {
			n += static_cast<u32>(b & 1u);
			b = static_cast<u8>(b >> 1u);
		}
	}
	return n;
}

} // namespace

int main() {
	MemorySystem mem = make_memory();
	graphics::composition::Scene scene {};
	const bool composed = graphics::composition::compose(
		scene, mem, graphics::composition::planar(320, 256, 4),
		graphics::composition::ocs_a500,
		graphics::composition::display(graphics::composition::kPal320x256,
					       graphics::composition::kBplcon0_4Planes));
	check(composed && scene.ok(), "compose() construye la escena");

	MockBackend backend {};
	TestGame game {};
	game.scene = &scene;
	App app {backend, game};

	app.run(1);

	check(game.inited, "el juego recibe init(App&)");
	check(game.updated, "el juego recibe update(App&)");
	check(game.rendered, "el juego recibe render(App&)");
	check(game.seen_frame == 0u, "app.frame() = 0 en el primer frame");

	// El `fill` de 4x4 color 1 deja 16 bits en el plano 0 y nada en los demas.
	const u8* p0 = scene.buffer(0).data();
	check(p0 != nullptr, "la escena tiene buffer");
	if (p0 != nullptr) {
		check(bits_in_plane(p0, scene.plane_bytes()) == 16u, "Screen::fill: 16 bits en el plano 0");
	}
	const u8* p1 = scene.buffer(0).data() + scene.plane_bytes();
	check(bits_in_plane(p1, scene.plane_bytes()) == 0u, "Screen::fill no toca el plano 1");

	if (failures == 0) {
		std::printf("OK: App/Screen (fachada de juego) validados.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
