// ============================================================================
// Test HOST-251: bucle reactivo del mini-SO sobre `eng::App`
// (eng/engine.hpp + eng/api/game.hpp + eng/os/port.hpp).
// ============================================================================
//
// Valida que:
//   1) El **hook de VBlank** del `Engine` publica un `MsgType::VBlank` por frame; el juego
//      lo consume en `update` y `app.vblank_count()` lo refleja.
//   2) Un **blit asincrono** (`App::blitter_memcpy_async`) notifica `MsgType::BlitDone` por
//      el puerto (callback inmediato en el mock) y sube `app.blitdone_count()`.
//   3) `pump()` drena lo no consumido y no rompe el consumo del juego.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/251_reactive_loop

#include <cstdio>

#include <eng/api/game.hpp>
#include <eng/os/port.hpp>

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

/// Backend de prueba **polling** (sin `set_vblank_service`): el engine cae a
/// `run_frames_polling`, que llama al hook de VBlank una vez por frame. El blit asincrono
/// ejecuta el callback de inmediato (simula la IRQ BLIT).
struct MockBackend {
	void boot() {}
	void wait_vblank() {}
	template <class F, class P>
	void wait_vblank(F, P) {}

	template <class C>
	bool blitter_memcpy_async(Span<u8> dst, Span<const u8> src, void (*on_done)(C&, u16), C& user) {
		if (on_done == nullptr) {
			return false;
		}
		const usize n = dst.size() < src.size() ? dst.size() : src.size();
		for (usize i = 0; i < n; ++i) {
			dst[i] = src[i];
		}
		on_done(user, 0u);
		return true;
	}
};

struct TestGame {
	graphics::composition::Scene* scene = nullptr;
	u8 src[64] {};
	u8 dst[64] {};
	Span<u8> dst_span {};
	Span<const u8> src_span {};
	u32 vblank_seen = 0;
	u32 blit_seen = 0;
	bool started = false;
	bool started_once = false;

	void init(auto& app) {
		app.bind_scene(*scene);
		for (u32 i = 0; i < sizeof(src); ++i) {
			src[i] = static_cast<u8>(i * 5u + 1u);
			dst[i] = 0u;
		}
		dst_span = Span<u8> {dst, sizeof(dst)};
		src_span = Span<const u8> {src, sizeof(src)};
	}
	void update(auto& app) {
		os::Msg m;
		while (app.port().pop(m)) {
			if (m.type == os::MsgType::VBlank) {
				++vblank_seen;
			} else if (m.type == os::MsgType::BlitDone) {
				++blit_seen;
			}
		}
		if (!started_once) {
			started = app.blitter_memcpy_async(dst_span, src_span);
			started_once = true;
		}
	}
	void render(auto&) {}
};

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

	app.run(3);

	check(game.vblank_seen == 3u, "el juego consume 3 VBlank (uno por frame)");
	check(app.vblank_count() == 3u, "app.vblank_count() = 3");
	check(game.started, "arranca el blit asincrono");
	check(game.blit_seen == 1u, "el juego consume 1 BlitDone");
	check(app.blitdone_count() == 1u, "app.blitdone_count() = 1");
	check(app.port().empty(), "el puerto queda vacio (pump drena el resto)");

	bool copy_ok = true;
	for (u32 i = 0; i < sizeof(game.src); ++i) {
		if (game.dst[i] != game.src[i]) {
			copy_ok = false;
		}
	}
	check(copy_ok, "la copia asincrona es correcta");

	if (failures == 0) {
		std::printf("OK: bucle reactivo (VBlank + BlitDone por el puerto de App).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
