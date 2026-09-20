// Test host de `eng::graphics::scene::compose` (prototipo de composición de escenas):
// una escena planar se construye uniendo etapas (display + paleta + una etapa propia con
// un PatchHandle), sin una clase por driver.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/215_scene_compose

#include <cstdio>

#include <eng/graphics/scene/compose.hpp>

using namespace eng;

namespace {

alignas(16) u8 g_chip[192 * 1024];

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

} // namespace

int main() {
	MemorySystem mem = make_memory();
	u16 pal[4] = {0x000u, 0x123u, 0x456u, 0x789u};

	graphics::scene::Scene s;
	copper::PatchHandle sky {};
	const bool composed = graphics::scene::compose(
		s, mem, graphics::scene::planar4(320, 256, 4),
		graphics::scene::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x4200),
		graphics::scene::palette(eng::PaletteWords {pal, 4}, 0, 4),
		// Etapa propia: emite un MOVE parcheable (p. ej. el color de fondo por frame).
		[&](graphics::scene::Scene& sc) {
			sky = sc.scheduler().patchable(copper::Register::COLOR00, 0x0111);
		});

	check(composed, "compose() construye la escena");
	check(s.ok(), "la escena queda ok");
	check(s.bitplanes().data() != nullptr, "hay bitplanes");
	check(s.scheduler().words_used() > 0u, "la copperlist tiene palabras");

	check(sky.valid(), "la etapa emitio un PatchHandle valido");
	sky.set(0x0abcu);
	const u16* words = s.scheduler().data();
	check(words[sky.index + 1u] == 0x0abcu, "el PatchHandle parchea el color por frame");

	// Ciclo de vida: una tarea (FunctionRef) de frame ligada a la escena. El lambda debe
	// tener nombre (la referencia no lo copia).
	int frames = 0;
	auto frame_fn = [&]() { ++frames; };
	s.on_frame(frame_fn);
	s.tick();
	s.tick();
	check(frames == 2, "la tarea de frame corre una vez por tick");

	// Escena con etapas HAM/EHB: repeticion de filas (cuadruplicado) + zonas de paleta.
	graphics::scene::Scene s2;
	graphics::scene::SceneResources r2 = graphics::scene::planar4(320, 32, 4);
	r2.rows = 8; // 8 filas logicas x 4 = 32 lineas de display
	const graphics::scene::PaletteZone zones[2] = {
		graphics::scene::PaletteZone {0, eng::PaletteWords {pal, 4}, 0, 4},
		graphics::scene::PaletteZone {16, eng::PaletteWords {pal, 4}, 0, 4},
	};
	const bool ok2 = graphics::scene::compose(
		s2, mem, r2,
		graphics::scene::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x7a00),
		graphics::scene::palette_zones(eng::Span<const graphics::scene::PaletteZone> {zones, 2}),
		graphics::scene::row_repeat(4, 0x2c, 0x0022));
	check(ok2 && s2.ok(), "escena con row_repeat + zonas de paleta compone");
	check(s2.scheduler().words_used() > 0u, "la copperlist con row_repeat no esta vacia");

	// Preset `canvas` (interleaved): expone surface() para dibujar con primitivas.
	graphics::scene::Scene s3;
	const bool ok3 = graphics::scene::compose(s3, mem, graphics::scene::canvas(64, 64, 4),
						  graphics::scene::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x4200));
	check(ok3 && s3.ok(), "escena canvas (interleaved) compone");
	check(s3.playfield().bitplanes().data() != nullptr, "el playfield tiene bitplanes");
	field::Surface surf = s3.surface();
	const s16 xs[3] = {8, 40, 8};
	const s16 ys[3] = {8, 8, 40};
	check(surf.fill_polygon(xs, ys, 3, 3), "surface() pinta un poligono");

	// Preset ham: repeticion de filas + punteros inversos (caso fire-rgb).
	graphics::scene::Scene s4;
	const bool ok4 = graphics::scene::compose(
		s4, mem, graphics::scene::ham(320, 256, 8, 4),
		graphics::scene::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x7a00),
		graphics::scene::reverse_ptrs(),
		graphics::scene::row_repeat(4, 0x2c, 0x0022));
	check(ok4 && s4.ok(), "preset ham (row_repeat + reverse_ptrs) compone");

	if (failures == 0) {
		std::printf("OK: scene::compose (etapas display/paleta/zonas/row_repeat + PatchHandle + ciclo de vida).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
