// ============================================================================
// Test HOST-301: cursor por sprite de hardware (`eng::ui::HardwareCursor`).
// ============================================================================
//
// Valida en host (sin Amiga) la estructura DMA y la emisión del cursor:
//   1) `bind` valida el buffer (nullptr/corto -> false).
//   2) `set_bitmap` escribe DAT/DATB por línea y deja el terminador DMA nulo.
//   3) `set_position` codifica POS/CTL (VSTART/VSTOP/HSTART y alineación a par).
//   4) `emit_into` emite SPR0PTH/L -> estructura y DMACON con SPREN.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ui/301_ui_hardware_cursor

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/arena.hpp>
#include <eng/ui/hardware_cursor.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", m);
		++g_fail;
	}
}

alignas(16) eng::u8 g_chip[16 * 1024];

eng::MemorySystem make_memory() {
	eng::MemorySystem mem;
	mem.chip = eng::ChipArena {g_chip, sizeof(g_chip), eng::MemoryKind::Chip};
	return mem;
}

} // namespace

int main() {
	eng::MemorySystem mem = make_memory();
	auto blk = mem.chip.allocate_block<eng::SpriteTag>(eng::ui::HardwareCursor::kBytes + 16u, 16);
	check(blk.valid(), "allocate bloque Chip del cursor");

	eng::ui::HardwareCursor cur;
	check(!cur.bind(nullptr, 0u), "bind(nullptr) -> false");
	check(!cur.bind(blk.view.data(), 4u), "bind tamano corto -> false");
	check(cur.bind(blk.view.data(), eng::ui::HardwareCursor::kBytes), "bind ok");
	check(cur.valid(), "cursor valido");

	// --- bitmap ---
	eng::u16 dat[eng::ui::HardwareCursor::kSize];
	eng::u16 datb[eng::ui::HardwareCursor::kSize];
	for (eng::u16 i = 0u; i < eng::ui::HardwareCursor::kSize; ++i) {
		dat[i] = static_cast<eng::u16>(0x8000u >> i);
		datb[i] = static_cast<eng::u16>(0x0001u << i);
	}
	cur.set_bitmap(dat, datb);
	const eng::u16* w = cur.words();
	check(w[2] == dat[0] && w[3] == datb[0], "DAT/DATB fila 0");
	check(w[2u + 15u * 2u] == dat[15] && w[2u + 15u * 2u + 1u] == datb[15], "DAT/DATB fila 15");
	check(w[2u + 16u * 2u] == 0u && w[2u + 16u * 2u + 1u] == 0u, "terminador DMA nulo");

	// --- posicion (X par) ---
	cur.set_position(100, 50);
	check(w[0] == static_cast<eng::u16>((50u << 8) | (100u >> 1)), "POS (VSTART=50, HSTART=100)");
	check(w[1] == static_cast<eng::u16>((50u + 16u) << 8), "CTL (VSTOP=66)");
	check(cur.x() == 100 && cur.y() == 50, "x()/y()");

	// X impar se alinea a par en POS y pone el bit 0 de HSTART en CTL.
	cur.set_position(101, 50);
	check(w[0] == static_cast<eng::u16>((50u << 8) | (100u >> 1)), "POS con X impar alineada");
	check((w[1] & 1u) == 1u, "CTL bit 0 = HSTART[0] (X impar)");

	// --- emit_into ---
	auto cblk = mem.chip.allocate_block<eng::CopperTag>(1024u, 16u);
	eng::copper::Scheduler sched {cblk};
	cur.emit_into(sched);
	sched.end();
	check(sched.ok(), "scheduler ok");

	const eng::uintptr sp = reinterpret_cast<eng::uintptr>(blk.view.data());
	const eng::u16* cw = sched.data();
	const eng::u16 n = sched.words_used();
	bool spr0h = false;
	bool spr0l = false;
	bool spren = false;
	for (eng::u16 i = 0u; i + 1u < n; i += 2u) {
		if (cw[i] == 0x0120u && cw[i + 1u] == static_cast<eng::u16>(sp >> 16)) {
			spr0h = true;
		}
		if (cw[i] == 0x0122u && cw[i + 1u] == static_cast<eng::u16>(sp & 0xffffu)) {
			spr0l = true;
		}
		if (cw[i] == 0x0096u && (cw[i + 1u] & 0x0020u) != 0u) {
			spren = true;
		}
	}
	check(spr0h && spr0l, "SPR0PTH/L -> estructura del cursor");
	check(spren, "DMACON con SPREN");

	if (g_fail == 0) {
		std::printf("OK: cursor por sprite de hardware (estructura + emision) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es)\n", g_fail);
	return 1;
}
