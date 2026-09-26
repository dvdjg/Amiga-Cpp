// ============================================================================
// Test HOST-352: formateo del panel de telemetria + ChipStorage (Chip RAM estatica).
// ============================================================================
//
// Respalda `eng/debug/telemetry.hpp` (conversion decimal/fija/hex sin libc y `draw_telemetry`
// sobre un sink con `text()`) y `eng/memory/chip_storage.hpp` (`ChipStorage`: bufer estatico
// certificado en Chip RAM con vista de dominio y `Address<Chip>`).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/352_telemetry_chip

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/debug/telemetry.hpp>
#include <eng/memory/chip_storage.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

constexpr bool streq(const char* a, const char* b) noexcept {
	while (*a != '\0' && *a == *b) {
		++a;
		++b;
	}
	return *a == *b;
}

// Sink falso que captura las lineas dibujadas (misma firma que `DebugOverlay::text`).
struct FakeOverlay {
	char lines[8][40] {};
	int count = 0;
	void text(eng::s16, eng::s16, const char* s, eng::u32) {
		int i = 0;
		while (s[i] != '\0' && i < 39) {
			lines[count][i] = s[i];
			++i;
		}
		lines[count][i] = '\0';
		++count;
	}
};

// ChipStorage global con la macro de seccion (no-op en host; real en m68k).
ENG_CHIP_RAM eng::ChipStorage<eng::PlaneTag, 32> g_chip {};

} // namespace

int main() {
	std::printf("== HOST-352 telemetry/chip ==\n");

	// ChipStorage: vista de dominio + direccion chip certificada (en host, el tipo).
	check(g_chip.view().size() == 32u, "ChipStorage: vista con tamano");
	check(g_chip.address().valid(), "ChipStorage: direccion valida");
	check(g_chip.address().cptr() == g_chip.storage, "ChipStorage: apunta a su storage");
	g_chip.view().fill(0x5Au);
	check(g_chip.storage[0] == 0x5Au && g_chip.storage[31] == 0x5Au, "ChipStorage: vista escribe");

	// ChipView (MemView<Tag, Chip>): vista con el banco en el tipo (solo desde una fuente chip).
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> chip_block {
		eng::Bytes<eng::PlaneTag> {g_chip.storage, 32u}, eng::MemoryKind::Chip};
	const eng::ChipView<eng::PlaneTag> cv = chip_block.mem_view();
	check(cv.size() == 32u && cv.address().valid(), "ChipView: desde Block<Chip>");
	check(cv.address().cptr() == g_chip.storage, "ChipView: apunta al bloque");
	check(cv.subview(4, 8).size() == 8u, "ChipView: subview");
	check(cv.view().size() == 32u, "ChipView: vista de dominio");
	// El MISMO mecanismo sirve para otro banco (Fast): no hay un tipo por banco.
	eng::Block<eng::PlaneTag, eng::MemoryKind::Fast> fast_block {
		eng::Bytes<eng::PlaneTag> {g_chip.storage, 32u}, eng::MemoryKind::Fast};
	const eng::FastView<eng::PlaneTag> fv = fast_block.mem_view();
	check(fv.size() == 32u && fv.address().valid(), "FastView: mismo MemView, banco Fast");

	// Panel de telemetria.
	FakeOverlay o {};
	eng::debug::Telemetry t {};
	t.fps_x100 = 5960u;
	t.frames = 1234u;
	t.chip_used = 20480u;
	t.chip_capacity = 524288u;
	t.slow_used = 1024u;
	t.slow_capacity = 262144u;
	t.fast_used = 0u;
	t.fast_capacity = 0u;
	eng::debug::draw_telemetry(o, t, 4, 4, 8, 0xffffffu);
	check(o.count == 5, "panel: 5 lineas");
	check(streq(o.lines[0], "FPS 59.60"), "panel: fps");
	check(streq(o.lines[1], "FRAME 1234"), "panel: frame");
	check(streq(o.lines[2], "CHIP 20480/524288"), "panel: chip");
	check(streq(o.lines[3], "SLOW 1024/262144"), "panel: slow");
	check(streq(o.lines[4], "FAST 0/0"), "panel: fast");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: telemetria (formato + panel) y ChipStorage validados.\n");
	return 0;
}
