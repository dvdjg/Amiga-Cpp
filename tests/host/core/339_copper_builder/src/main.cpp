// ============================================================================
// Test HOST-339: fachada de Copper de alto nivel (eng::Copper).
// ============================================================================
//
// Respalda `eng/api/copper.hpp`: construye la copperlist por intencion (`wait_line`, `set_color`,
// `set_palette`, `set_scroll`) sobre un `copper::Scheduler`, sin nombrar WAIT/MOVE. Es el helper
// general para consumidores externos que quieran Copper de alto nivel.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/339_copper_builder

#include <cstdio>

#include <eng/api/copper.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/memory/arena.hpp>

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-339 copper_builder ==\n");

	eng::u16 buf[256] {};
	eng::Block<eng::CopperTag> blk {
		eng::Bytes<eng::CopperTag> {reinterpret_cast<eng::u8*>(buf), sizeof(buf)},
		eng::MemoryKind::Chip};
	eng::copper::Scheduler sched {blk};
	eng::Copper c {sched};

	const eng::u16 w0 = c.words_used();
	const eng::u16 pal[2] = {0x0f00u, 0x00f0u};
	c.set_palette(eng::PaletteWords {pal, 2u}, 0u, 2u);
	c.wait_line(120u);
	c.set_color(1u, 0x0aaau);
	c.set_scroll(0x1234u);

	check(c.words_used() > w0, "emite palabras de Copper");
	check(c.words_used() <= 256u, "cabe en el presupuesto");

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: Copper (wait_line/set_color/set_palette/set_scroll) validado.\n");
	return 0;
}
