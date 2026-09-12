// ============================================================================
// Test HOST-019: extensiones de copper para "copper chunky" (SKIP, MOVE32, patch).
// ============================================================================
//
// Valida la codificacion EXACTA de libgfx (demoscene): `CopSkip` (WAIT con mascara
// 0xffff), `CopMove32` (reg+2 low, luego reg high) y el parcheo de instrucciones
// (equivalente a guardar el CopInsT* y hacer CopSetColor). Base del porte de plasma.
//
//   bash tools/run-host-tests.sh tests/host/019_copper_ext

#include <cstdio>

#include <eng/core/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/memory/arena.hpp>

namespace {

using eng::u16;
using eng::u32;
using eng::MemoryBlock;
using eng::MemoryKind;
using eng::copper::ListBuilder;
using eng::copper::Register;

static eng::u16 g_words[128];

} // namespace

int main() {
	MemoryBlock block { g_words, sizeof(g_words), MemoryKind::Chip };
	ListBuilder b { block };

	// --- SKIP: word0 = (vpos<<8)|((hpos>>1)|1), word1 = 0xffff ---
	const u16 skip_ins = b.skip(0x2cu, 0xd2u); // hpos color-clock 0xD2
	if (g_words[skip_ins] != 0x2c69u || g_words[skip_ins + 1u] != 0xffffu) {
		std::printf("[FAIL] skip: %04x %04x (esperado 2c69 ffff)\n",
			    g_words[skip_ins], g_words[skip_ins + 1u]);
		return 1;
	}
	// Parcheo del word de valor (como CopSetColor por frame).
	b.patch_data(skip_ins, 0x1234u);
	if (g_words[skip_ins + 1u] != 0x1234u) {
		std::printf("[FAIL] patch_data\n");
		return 1;
	}

	// --- MOVE32: reg+2 (low) y luego reg (high) ---
	const u32 addr = 0x00c09abcu;
	const u16 m32 = b.move32(Register::COP2LCH, reinterpret_cast<const void*>(static_cast<eng::uintptr>(addr)));
	if (g_words[m32] != 0x0086u || g_words[m32 + 1u] != 0x9abcu ||
	    g_words[m32 + 2u] != 0x0084u || g_words[m32 + 3u] != 0x00c0u) {
		std::printf("[FAIL] move32: %04x %04x %04x %04x (esperado 0086 9abc 0084 00c0)\n",
			    g_words[m32], g_words[m32 + 1u], g_words[m32 + 2u], g_words[m32 + 3u]);
		return 1;
	}

	// --- move_at + patch (patron del plasma: una instruccion COLOR por bloque) ---
	const u16 color_ins = b.move_at(Register::COLOR00, 0x0000u);
	b.patch_data(color_ins, 0x0f0fu);
	if (g_words[color_ins] != 0x0180u || g_words[color_ins + 1u] != 0x0f0fu) {
		std::printf("[FAIL] move_at/patch COLOR00: %04x %04x\n", g_words[color_ins], g_words[color_ins + 1u]);
		return 1;
	}

	// --- COPJMP2 (MOVE16 simple) ---
	const u16 jmp = b.move_at(Register::COPJMP2, 0x7fffu);
	if (g_words[jmp] != 0x008au) {
		std::printf("[FAIL] COPJMP2 reg offset %04x\n", g_words[jmp]);
		return 1;
	}

	b.end();
	if (!b.ok()) {
		std::printf("[FAIL] builder no ok\n");
		return 1;
	}

	std::printf("OK: copper chunky (SKIP 0xffff, MOVE32 reg+2/reg, patch) validado.\n");
	return 0;
}
