// ============================================================================
// Test HOST-019: extensiones de copper para "copper chunky" (SKIP, MOVE32, patch).
// ============================================================================
//
// Valida la codificacion EXACTA de libgfx (demoscene): `CopSkip` (WAIT con mascara
// 0xffff), `CopMove32` (reg+2 low, luego reg high) y el parcheo de instrucciones
// (equivalente a guardar el CopInsT* y hacer CopSetColor). Base del porte de plasma.
//
//   bash tools/run-host-tests.sh tests/host/graphics/019_copper_ext

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/template.hpp>
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
	const u16 m32 = b.move32(Register::COP2LCH, eng::ChipAddress { addr });
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

	// --- Template: estructura una vez + parcheo de datos/linea por frame ---
	{
		eng::u16 tw[64];
		MemoryBlock tb {tw, sizeof(tw), MemoryKind::Chip};
		eng::copper::Template t {tb};
		const u16 c1 = t.move_slot(static_cast<u16>(0x182u), 0x0111u); // COLOR01
		const u16 w1 = t.wait_slot(0x2cu);
		const u16 c2 = t.move_slot(static_cast<u16>(0x184u), 0x0222u); // COLOR02
		t.end();
		if (!t.ok()) {
			std::printf("[FAIL] template no ok\n");
			return 1;
		}
		// Estructura: WAIT(0x2c) luego MOVE COLOR02.
		if (tw[w1] != 0x2c01u || tw[c2] != 0x0184u || tw[c2 + 1u] != 0x0222u) {
			std::printf("[FAIL] template estructura: %04x %04x %04x\n", tw[w1], tw[c2],
				    tw[c2 + 1u]);
			return 1;
		}
		// Parcheo por frame: 1 palabra por slot.
		t.set(c1, 0xa1a1u);
		t.set(c2, 0xb2b2u);
		t.set_wait(w1, 0x50u);
		t.set_reg(c2, 0x0190u); // reasigna el destino de c2 a COLOR08
		if (tw[c1 + 1u] != 0xa1a1u || tw[c2 + 1u] != 0xb2b2u || tw[w1] != 0x5001u ||
		    tw[c2] != 0x0190u) {
			std::printf("[FAIL] template patch: %04x %04x %04x %04x\n", tw[c1 + 1u],
				    tw[c2 + 1u], tw[w1], tw[c2]);
			return 1;
		}
	}

	// --- Template "por bandas": grupo de waits que se desplaza + datos variables ---
	{
		eng::u16 bw[128];
		MemoryBlock band_block {bw, sizeof(bw), MemoryKind::Chip};
		eng::copper::Template t {band_block};
		u16 wait[3];
		u16 col[3];
		for (int k = 0; k < 3; ++k) {
			wait[k] = t.wait_slot(static_cast<u16>(0x2cu + 8 * k));
			col[k] = t.move_slot(static_cast<u16>(0x182u), 0);
		}
		t.end();
		// Frame: el grupo se desplaza 3 lineas y los datos cambian (2 palabras/slot).
		for (int k = 0; k < 3; ++k) {
			t.set_wait(wait[k], static_cast<u16>(0x2cu + 3 + 8 * k));
			t.set(col[k], static_cast<u16>(0x0100u + k));
		}
		if (bw[wait[0]] != 0x2f01u || bw[wait[2]] != 0x3f01u || bw[col[2] + 1u] != 0x0102u) {
			std::printf("[FAIL] template bandas: %04x %04x %04x\n", bw[wait[0]], bw[wait[2]],
				    bw[col[2] + 1u]);
			return 1;
		}
	}

	std::printf("OK: copper chunky (SKIP 0xffff, MOVE32 reg+2/reg, patch, Template) validado.\n");
	return 0;
}