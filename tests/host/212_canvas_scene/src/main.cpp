// Test host de `eng::graphics::drivers::CanvasScene`: driver planar que expone una
// `Surface` de dibujo sobre un `CanvasPlayfield` (bitmap interleaved) + su copperlist.
// Cierra el hueco «efecto -> dibujo con Surface».
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/212_canvas_scene

#include <cstdio>

#include <eng/graphics/drivers/canvas_scene.hpp>

using namespace eng;
using namespace eng::graphics::drivers;

namespace {

struct MockBackend {
	const u16* taken = nullptr;
	const u16* installed = nullptr;
	void takeover_display(const u16* words) { taken = words; }
	void install_copper_list(const u16* words) { installed = words; }
};

// Cumple el contrato completo de driver grafico.
static_assert(eng::GraphicsDriver<CanvasScene, MockBackend>);
static_assert(eng::DisplayDriver<CanvasScene, MockBackend>);

alignas(16) u8 g_chip[256 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = LinearArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

/// Color (0..15) de un pixel del bitmap interleaved del lienzo.
u8 color_at(const field::CanvasPlayfield& pf, s32 x, s32 y) {
	const u8* base = pf.bitmap().bytes().data();
	const u32 row = pf.bitmap().row_bytes();
	const u32 planes = pf.planes();
	const u32 byte = static_cast<u32>(y) * planes * row +
			 (static_cast<u32>(x / 8) & ~1u);
	const u16 mask = static_cast<u16>(0x8000u >> (x & 15));
	u8 c = 0;
	for (u8 p = 0; p < planes; ++p) {
		const u16 v = *reinterpret_cast<const u16*>(base + byte + static_cast<u32>(p) * row);
		if ((v & mask) != 0u) c |= static_cast<u8>(1u << p);
	}
	return c;
}

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

} // namespace

int main() {
	MemorySystem mem = make_memory();
	CanvasScene scene;
	CanvasSceneConfig cfg {};
	cfg.width = 320;
	cfg.height = 256;
	cfg.planes = 4;

	check(scene.init(mem, cfg), "CanvasScene::init reserva bitmap + copperlist");
	check(scene.ok(), "CanvasScene::ok tras init");
	check(scene.copper_words() > 0u, "la copperlist tiene palabras");

	field::Surface surf = scene.surface();
	// Cuadrado relleno con color 5.
	const s16 xs[4] = {40, 120, 120, 40};
	const s16 ys[4] = {40, 40, 120, 120};
	check(surf.fill_polygon(xs, ys, 4, 5), "Surface::fill_polygon sobre la escena");
	check(color_at(scene.playfield(), 80, 80) == 5u, "interior con el color pedido");
	check(color_at(scene.playfield(), 10, 10) == 0u, "fuera del cuadrado vacio");

	MockBackend backend;
	scene.takeover(backend);
	check(backend.taken == scene.copper_words_ptr(), "takeover instala la copperlist");
	scene.install(backend);
	check(backend.installed == scene.copper_words_ptr(), "install hace el swap");

	if (failures == 0) {
		std::printf("OK: CanvasScene (Surface sobre CanvasPlayfield + copperlist interleaved).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
