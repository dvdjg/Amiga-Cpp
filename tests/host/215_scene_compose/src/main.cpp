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

alignas(16) u8 g_chip[64 * 1024];

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

	if (failures == 0) {
		std::printf("OK: scene::compose (escena por etapas + PatchHandle).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
