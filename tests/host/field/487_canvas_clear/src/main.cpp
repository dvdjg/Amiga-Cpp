// ============================================================================
// Test HOST-487: el motor deja limpios los lienzos/HUD al reservarlos.
// ============================================================================
//
// Cubre el contrato que evita que una demo limpie Chip RAM a mano:
//   - `CanvasPlayfield::begin` arranca con todos los bytes a cero;
//   - `CanvasPlayfield::clear(color)` deja un color determinista y `clear()` negro;
//   - `HudStrip::bind` limpia la franja aunque el bloque externo llegue sucio.
//
// Memoria Chip sintética; sin WinUAE ni backend.

#include <cstdio>
#include <cstring>

#include <eng/field/canvas_playfield.hpp>
#include <eng/hud/strip.hpp>
#include <eng/memory/memory_manager.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

bool all_zero(const eng::u8* data, eng::u32 bytes) {
	for (eng::u32 i = 0u; i < bytes; ++i) {
		if (data[i] != 0u) return false;
	}
	return true;
}

alignas(16) eng::u8 g_chip[128u * 1024u];

} // namespace

int main() {
	eng::MemoryManager mem {};
	check(mem.configure(g_chip, sizeof(g_chip), nullptr, 0u, nullptr, 0u, 16u),
	      "MemoryManager Chip");

	eng::playfield::CanvasPlayfield canvas {};
	const eng::playfield::CanvasPlayfield::Config cfg {64u, 8u, 4u, 0u, 0u};
	check(canvas.begin(mem, cfg), "CanvasPlayfield::begin");
	const eng::u8* base = canvas.bitmap().chip_planes().data();
	const eng::u32 bytes = canvas.bitmap().total_bytes();
	check(all_zero(base, bytes), "begin deja el lienzo a cero");

	check(canvas.fill_rect_hw(0, 0, 64u, 8u, 5u), "fill_rect_hw");
	check(!all_zero(base, bytes), "el relleno ensucia el lienzo");
	canvas.clear();
	check(all_zero(base, bytes), "clear() vuelve a negro");

	// Franja externa sucia: `HudStrip::bind` debe limpiarla.
	auto block = mem.chip().template reserve<eng::PlaneTag>(64u, 16u);
	check(block.valid(), "reserva Chip de la franja");
	eng::u8* block_data = const_cast<eng::u8*>(block.view.data());
	__builtin_memset(block_data, 0xaa, 64u);

	eng::hud::HudStrip strip {};
	const eng::hud::HudStripConfig strip_cfg {32u, 8u, 2u, 0u};
	check(strip.bind(block, strip_cfg), "HudStrip::bind");
	check(all_zero(block_data, 64u), "HudStrip::bind limpia la franja");

	if (g_fail == 0) {
		std::printf("HOST-487 canvas_clear: OK\n");
		return 0;
	}
	std::printf("HOST-487 canvas_clear: %d fallos\n", g_fail);
	return 1;
}
