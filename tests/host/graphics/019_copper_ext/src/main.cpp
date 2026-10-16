// ============================================================================
// Test HOST-019: extensiones de copper para "copper chunky" (SKIP, MOVE32, patch).
// ============================================================================
//
// Valida la codificacion EXACTA de libgfx (demoscene): `CopSkip` (WAIT con mascara
// 0xffff), `CopMove32` (reg+2 low, luego reg high) y el parcheo de instrucciones
// (equivalente a guardar el CopInsT* y hacer CopSetColor). Base del porte de plasma.
//
// Con el modelo tipado (`MoveSlot`/`WaitSlot`), los parches de MOVE se hacen por slot:
// `patch_data(slot, ...)` no acepta el indice de un WAIT (no compila) y viceversa.
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

	// --- SKIP: word0 = (vpos<<8)|((hpos>>1)|1), word1 = 0xffff (bit 0 = SKIP) ---
	const eng::copper::WaitSlot skip_ins = b.skip(0x2cu, 0xd2u); // hpos color-clock 0xD2
	const u16 skip_index = skip_ins; // lectura del indice (el tipo distingue MOVE de WAIT/SKIP)
	if (g_words[skip_index] != 0x2c69u || g_words[skip_index + 1u] != 0xffffu) {
		std::printf("[FAIL] skip: %04x %04x (esperado 2c69 ffff)\n",
			    g_words[skip_index], g_words[skip_index + 1u]);
		return 1;
	}

	// --- MOVE32: reg+2 (low) y luego reg (high) ---
	const u32 addr = 0x00c09abcu;
	const eng::copper::MoveSlot m32 = b.move32(Register::COP2LCH, eng::Address<eng::MemoryKind::Chip> { addr });
	const u16 m32_index = m32;
	if (g_words[m32_index] != 0x0086u || g_words[m32_index + 1u] != 0x9abcu ||
	    g_words[m32_index + 2u] != 0x0084u || g_words[m32_index + 3u] != 0xc0u) {
		std::printf("[FAIL] move32: %04x %04x %04x %04x (esperado 0086 9abc 0084 00c0)\n",
			    g_words[m32_index], g_words[m32_index + 1u], g_words[m32_index + 2u],
			    g_words[m32_index + 3u]);
		return 1;
	}

	// --- move_at + patch_data (patron del plasma: una instruccion COLOR por bloque) ---
	const eng::copper::MoveSlot color_ins = b.move_at(Register::COLOR00, 0x0000u);
	b.patch_data(color_ins, 0x0f0fu);
	if (g_words[color_ins.opcode_word()] != 0x0180u ||
	    g_words[color_ins.data_word()] != 0x0f0fu) {
		std::printf("[FAIL] move_at/patch COLOR00: %04x %04x\n", g_words[color_ins.opcode_word()],
			    g_words[color_ins.data_word()]);
		return 1;
	}
	// `patch_move_reg` reescribe el DESTINO del MOVE por slot (sin tocar el dato).
	b.patch_move_reg(color_ins, eng::copper::color_register(1u));
	if (g_words[color_ins.opcode_word()] != eng::copper::color_register(1u).offset ||
	    g_words[color_ins.data_word()] != 0x0f0fu) {
		std::printf("[FAIL] patch_move_reg: %04x %04x\n", g_words[color_ins.opcode_word()],
			    g_words[color_ins.data_word()]);
		return 1;
	}

	// --- COPJMP2 (MOVE16 simple) ---
	const eng::copper::MoveSlot jmp = b.move_at(Register::COPJMP2, 0x7fffu);
	if (g_words[jmp.opcode_word()] != 0x008au) {
		std::printf("[FAIL] COPJMP2 reg offset %04x\n", g_words[jmp.opcode_word()]);
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
		const eng::copper::MoveSlot c1 = t.move_slot(eng::copper::color_register(1u), 0x0111u); // COLOR01
		const eng::copper::WaitSlot w1 = t.wait_slot(0x2cu);
		const eng::copper::MoveSlot c2 = t.move_slot(eng::copper::color_register(2u), 0x0222u); // COLOR02
		t.end();
		if (!t.ok()) {
			std::printf("[FAIL] template no ok\n");
			return 1;
		}
		// Estructura: WAIT(0x2c) luego MOVE COLOR02.
		if (tw[w1.opcode_word()] != 0x2c01u || tw[c2.opcode_word()] != 0x0184u ||
		    tw[c2.data_word()] != 0x0222u) {
			std::printf("[FAIL] template estructura: %04x %04x %04x\n", tw[w1.opcode_word()],
				    tw[c2.opcode_word()], tw[c2.data_word()]);
			return 1;
		}
		// Parcheo por frame: 1 palabra por slot.
		t.set(c1, 0xa1a1u);
		t.set(c2, 0xb2b2u);
		t.set_wait(w1, 0x50u);
		t.set_reg(c2, eng::copper::color_register(8u)); // reasigna el destino de c2 a COLOR08
		if (tw[c1.data_word()] != 0xa1a1u || tw[c2.data_word()] != 0xb2b2u ||
		    tw[w1.opcode_word()] != 0x5001u || tw[c2.opcode_word()] != 0x0190u) {
			std::printf("[FAIL] template patch: %04x %04x %04x %04x\n", tw[c1.data_word()],
				    tw[c2.data_word()], tw[w1.opcode_word()], tw[c2.opcode_word()]);
			return 1;
		}
	}

	// --- Template "por bandas": grupo de waits que se desplaza + datos variables ---
	{
		eng::u16 bw[128];
		MemoryBlock band_block {bw, sizeof(bw), MemoryKind::Chip};
		eng::copper::Template t {band_block};
		eng::copper::WaitSlot wait[3];
		eng::copper::MoveSlot col[3];
		for (int k = 0; k < 3; ++k) {
			wait[k] = t.wait_slot(static_cast<u16>(0x2cu + 8 * k));
			col[k] = t.move_slot(eng::copper::color_register(1u), 0);
		}
		t.end();
		// Frame: el grupo se desplaza 3 lineas y los datos cambian (2 palabras/slot).
		for (int k = 0; k < 3; ++k) {
			t.set_wait(wait[k], static_cast<u16>(0x2cu + 3 + 8 * k));
			t.set(col[k], static_cast<u16>(0x0100u + k));
		}
		if (bw[wait[0].opcode_word()] != 0x2f01u || bw[wait[2].opcode_word()] != 0x3f01u ||
		    bw[col[2].data_word()] != 0x0102u) {
			std::printf("[FAIL] template bandas: %04x %04x %04x\n", bw[wait[0].opcode_word()],
				    bw[wait[2].opcode_word()], bw[col[2].data_word()]);
			return 1;
		}
	}

	std::printf("OK: copper chunky (SKIP 0xffff, MOVE32 reg+2/reg, patch, Template) validado.\n");
	return 0;
}
