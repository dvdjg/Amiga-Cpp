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

/// Backend minimo: el ciclo del bucle, un `MemoryManager` con Chip (lo piden `add_scroll_layer`
/// y `present_scene`) y `takeover_display` (lo pide el display por bandas).
struct MockBackend {
	alignas(16) eng::u8 m_chip[16u * 1024u] {};
	eng::MemoryManager m_mm {};
	int takeovers = 0;
	const eng::u16* last_copper = nullptr;
	MockBackend() { (void)m_mm.configure(m_chip, sizeof(m_chip), nullptr, 0u, nullptr, 0u, 16u); }
	eng::MemoryManager& memory_manager() { return m_mm; }
	void takeover_display(const eng::u16* copper_words) {
		++takeovers;
		last_copper = copper_words;
	}
	void boot() {}
	void wait_vblank() {}
	template <class F, class P>
	void wait_vblank(F, P) {}
};

/// Capa de scroll mock que implementa la interfaz `ScrollLayer`: `App` la arranca (`begin`) y la
/// conduce por frame (`frame`). Memoria **tipada** (`MemoryManager&`), backend por su tipo real.
struct MockScroll : eng::playfield::ScrollLayer<MockBackend> {
	int begins = 0;
	int frames = 0;
	bool begin(eng::MemoryManager&, MockBackend&) noexcept override {
		++begins;
		return true;
	}
	void frame(MockBackend&) noexcept override { ++frames; }
};

/// Escalera mock: devuelve siempre el motor registrado (para probar `App::pick_scroll_engine`).
struct MockLadder {
	eng::Ref<eng::playfield::ScrollLayer<MockBackend>> engine {};
	eng::Ref<eng::playfield::ScrollLayer<MockBackend>>
	pick(const eng::playfield::RuntimeScrollGeometry&) noexcept {
		return engine;
	}
};

/// Capa de BOBs mock: solo devuelve cuántas bandas recibió (para probar `App::emit_bobs_banded`).
struct MockBobs {
	eng::u16 emit_banded(eng::graphics::FramePlan&, eng::Span<const eng::scene::BandSpan> bands,
			     eng::Span<const eng::graphics::BobTarget>, eng::Span<const eng::u8>) {
		return static_cast<eng::u16>(bands.size());
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

		// Capa con **rol/colocación** (planner §7): el `App` forma el plan de escena → estrategia.
		MockScroll bg {};
		check(app.add_scroll_layer(bg, eng::scene::LayerRole::Foreground,
					   eng::scene::LayerPlacement {}),
		      "add_scroll_layer con rol");
		check(app.scene_plan().count() == 1u, "el plan de escena tiene 1 capa");
		check(app.scene_strategy() == eng::scene::SceneStrategy::Single, "estrategia Single (1 capa)");

		// Tramos de banda del plan (split-screen): 2 capas en banda → `scene_bands` da 2.
		MockBackend b2 {};
		SceneGame g2 {};
		App app2 {b2, g2};
		eng::GameDisplay disp {};
		disp.width = 320u;
		disp.height = 256u;
		(void)app2.set_display(disp);
		MockScroll top {}, bot {};
		(void)app2.add_scroll_layer(top, eng::scene::LayerRole::Foreground,
					    eng::scene::LayerPlacement {0u, 128u, 0u});
		(void)app2.add_scroll_layer(bot, eng::scene::LayerRole::Foreground,
					    eng::scene::LayerPlacement {128u, 128u, 0u});
		eng::scene::BandSpan bs[4] {};
		check(app2.scene_bands(eng::Span<eng::scene::BandSpan> {bs, 4u}) == 2u,
		      "scene_bands → 2 tramos");
		check(bs[1].top == 128u, "tramo 1 en top=128");
		check(app2.scene_strategy() == eng::scene::SceneStrategy::Bands, "estrategia Bands");

		// `emit_bobs_banded`: usa los tramos del plan (2) o el display completo (1) si no hay plan.
		MockBobs mb {};
		check(app2.emit_bobs_banded(mb, {}) == 2u, "emit_bobs_banded usa el plan (2 bandas)");
		check(app.emit_bobs_banded(mb, {}) == 1u, "sin plan de bandas → 1 banda (pantalla)");

		// `pick_scroll_engine`: la escalera elige por geometría; el App registra/arranca el motor.
		MockBackend b3 {};
		SceneGame g3 {};
		App app3 {b3, g3};
		MockScroll chosen {};
		MockLadder ladder {};
		ladder.engine = chosen;
		const auto geo = eng::playfield::runtime_scroll_geometry(320u, 256u, 3u, 16u, 16u);
		check(geo.has_value(), "geometría válida");
		check(app3.pick_scroll_engine(ladder, *geo), "pick_scroll_engine registra el motor elegido");
		check(chosen.begins == 1, "el motor elegido quedó arrancado");
	}
	{
		// `present_scene`: el App materializa un `RasterLayout` (bandas) y toma el display.
		MockBackend backend {};
		SceneGame game {};
		App app {backend, game};
		eng::scene::Band bands[2] {};
		bands[0].planes = 0u; // franja sin DMA de planos (solo cabecera)
		bands[1].top = 128u;
		bands[1].planes = 0u; // tramo conmutado a top=128
		check(app.present_scene(bands), "present_scene materializa 2 bandas y toma el display");
		check(backend.takeovers == 1, "present_scene llama takeover_display una vez");
		check(backend.last_copper != nullptr, "present_scene entrega una copperlist válida");

		// Sin bandas → no toca el display.
		const int before = backend.takeovers;
		check(!app.present_scene(eng::Span<const eng::scene::Band> {}), "sin bandas devuelve false");
		check(backend.takeovers == before, "sin bandas no hace takeover");
	}

	if (failures == 0) {
		std::printf("OK: pila de escenas (push/pop/set + dispatch) validada.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
