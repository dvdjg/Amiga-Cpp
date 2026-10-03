// ============================================================================
// Test HOST-240: pila de escenas de la fachada de juego (`eng::App`).
// ============================================================================
//
// Valida `App::push_scene/pop_scene/set_scene`: la escena superior **sustituye** al `Game` en
// `update`/`render`, `enter`/`exit` se llaman en las transiciones, los hooks opcionales se detectan
// con `requires` y la capacidad es fija (sin heap).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/240_app_scenes

#include <cstdio>

#include <eng/api/api.hpp>
#include <eng/memory/memory_manager.hpp>

using namespace eng;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

/// Backend minimo: el ciclo del bucle y un `MemoryManager` (lo pide `add_scroll_layer`).
struct MockBackend {
	eng::MemoryManager m_mm {};
	eng::MemoryManager& memory_manager() { return m_mm; }
	void boot() {}
	void wait_vblank() {}
	template <class F, class P>
	void wait_vblank(F, P) {}
};

/// Capa de scroll mock (type-erased): `App` la arranca (`begin`) y la conduce por frame (`frame`).
struct MockScroll {
	int begins = 0;
	int frames = 0;
	eng::ScrollLayerHandle handle() noexcept {
		eng::ScrollLayerHandle h {};
		h.obj = this;
		h.begin = [](void* o, void*, void*) -> bool {
			++static_cast<MockScroll*>(o)->begins;
			return true;
		};
		h.frame = [](void* o, void*) { ++static_cast<MockScroll*>(o)->frames; };
		return h;
	}
};

/// El `Game` es el *composition root*: solo empuja escenas; sin escena activa conduce el frame.
struct SceneGame {
	int inits = 0;
	int updates = 0;
	int renders = 0;
	void init(auto&) { ++inits; }
	void update(auto&) { ++updates; }
	void render(auto&) { ++renders; }
};

struct TestScene {
	int enters = 0;
	int exits = 0;
	int updates = 0;
	int renders = 0;
	void enter(auto&) { ++enters; }
	void exit(auto&) { ++exits; }
	void update(auto&) { ++updates; }
	void render(auto&) { ++renders; }
};

/// Escena sin `enter`/`exit`: sus hooks opcionales deben detectarse con `requires`.
struct MinimalScene {
	int updates = 0;
	int renders = 0;
	void update(auto&) { ++updates; }
	void render(auto&) { ++renders; }
};

} // namespace

int main() {
	{
		MockBackend backend {};
		SceneGame game {};
		App app {backend, game};

		check(app.scene_depth() == 0u, "pila de escenas vacia al arrancar");
		app.run(1u);
		check(game.updates == 1 && game.renders == 1, "sin escena, update/render van al Game");

		TestScene s1 {};
		check(app.push_scene(s1), "push_scene acepta una escena");
		check(s1.enters == 1, "push_scene llama enter de la escena");
		check(app.scene_depth() == 1u, "la profundidad refleja la escena empujada");
		app.run(1u);
		check(s1.updates == 1 && s1.renders == 1, "la escena conduce update/render");
		check(game.updates == 1 && game.renders == 1, "el Game no corre mientras hay escena activa");

		TestScene s2 {};
		check(app.push_scene(s2), "push_scene apila una segunda escena");
		check(s2.enters == 1, "la segunda escena recibe enter");
		app.run(1u);
		check(s1.updates == 1 && s2.updates == 1, "solo la escena superior conduce el frame");

		check(app.pop_scene(), "pop_scene saca la escena superior");
		check(s2.exits == 1, "pop_scene llama exit de la escena sacada");
		app.run(1u);
		check(s1.updates == 2, "al sacar la superior vuelve a conducir la anterior");

		check(app.set_scene(s2), "set_scene reemplaza la pila");
		check(s1.exits == 1 && s2.exits == 1 && s2.enters == 2,
		      "set_scene hace exit de las existentes y enter de la nueva");
		app.pop_scene();
		app.run(1u);
		check(game.updates == 2 && game.renders == 2, "sin escenas vuelve a conducir el Game");
		check(!app.pop_scene(), "pop_scene sobre pila vacia devuelve false");
	}
	{
		// Escena minima (hooks opcionales) + capacidad fija.
		MockBackend backend {};
		SceneGame game {};
		App app {backend, game};
		MinimalScene m {};
		check(app.push_scene(m), "una escena sin enter/exit se acepta (hooks opcionales)");
		app.run(1u);
		check(m.updates == 1 && m.renders == 1, "la escena minima conduce el frame");
		check(app.pop_scene(), "la escena minima se puede sacar");

		MinimalScene pool[9] {};
		bool all_pushed = true;
		for (int i = 0; i < 8; ++i) all_pushed = all_pushed && app.push_scene(pool[i]);
		check(all_pushed && app.scene_depth() == 8u, "se apilan hasta la capacidad fija");
		check(!app.push_scene(pool[8]), "empujar por encima de la capacidad falla (sin heap)");
	}
	{
		// Backend sin `assets()`/`audio()`: `play_music`/`stop_music` son no-op seguros (templates).
		MockBackend backend {};
		SceneGame game {};
		App app {backend, game};
		check(!app.play_music("tema"), "play_music sin assets/audio devuelve false");
		app.stop_music();
		check(true, "stop_music sin audio no falla");
	}
	{
		// Capas de scroll: `App` las arranca (begin) y las conduce por frame (pump en el update).
		MockBackend backend {};
		SceneGame game {};
		App app {backend, game};
		MockScroll scroll {};
		check(app.scroll_layer_count() == 0u, "sin capas de scroll al arrancar");
		check(app.add_scroll_layer(scroll), "add_scroll_layer acepta la capa");
		check(scroll.begins == 1, "add_scroll_layer arranca la capa (begin)");
		check(app.scroll_layer_count() == 1u, "la capa queda registrada");
		app.run(1u);
		check(scroll.frames >= 1, "App conduce la capa por frame (frame/pump)");
	}

	if (failures == 0) {
		std::printf("OK: pila de escenas (push/pop/set + dispatch) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
