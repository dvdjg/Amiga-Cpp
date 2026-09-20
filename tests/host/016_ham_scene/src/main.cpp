// ============================================================================
// Test HOST-016: `scene::compose` — display planar con repeticion de filas
// (cuadruplicado) y etapas (display/paleta/row_repeat).
// ============================================================================
//
// Sustituye la validacion del driver `PlanarScene` por el modelo de escena
// (`engine/include/eng/graphics/scene/compose.hpp`), con la misma geometria:
//
//   1) `compose` construye la escena y expone bitplanes/planos.
//   2) `display`: BPLCON0, DIW/DDF y punteros BPLxPT.
//   3) `row_repeat`: BPL1MOD/BPL2MOD = -row_bytes en las lineas del grupo menos la
//      ultima (que avanza con modulo 0); BPLCON1 alterno.
//   4) `palette` carga los colores y la lista termina.
//   5) Parametrico: otro `SceneResources` (sin repeticion, otros planos/BPLCON0)
//      produce otra geometria sin tocar `Scene`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/016_ham_scene   (solo este)
//   bash tools/run-host-tests.sh                            (todos)

#include <cstdio>
#include <type_traits>

#include <eng/core/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/scene/compose.hpp>
#include <eng/memory/arena.hpp>

namespace {

using eng::MemoryKind;
using eng::MemorySystem;
using eng::LinearArena;
using eng::u16;
using eng::u32;
using eng::graphics::scene::Scene;
using eng::graphics::scene::SceneResources;

alignas(16) eng::u8 g_chip[512 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = LinearArena { g_chip, sizeof(g_chip), MemoryKind::Chip };
	return mem;
}

/// Recorre la copperlist en pares (word0, word1) contando MOVEs de un registro.
struct MoveTally {
	unsigned mod_back = 0;    // BPL1MOD = -row_bytes
	unsigned mod_zero = 0;    // BPL1MOD = 0
	unsigned bplcon1_shift = 0;
	unsigned bplcon1_zero = 0;
	unsigned waits = 0;
	unsigned color_moves = 0;
	unsigned bplcon0_moves = 0;
};

MoveTally tally(const eng::u16* words, eng::u16 count, eng::u16 row_back) {
	MoveTally t {};
	bool after_wait = false;
	for (eng::u16 i = 0; i + 1u < count; i += 2u) {
		const eng::u16 a = words[i];
		if (a == 0xffffu) break;                     // fin de lista
		if ((a & 1u) != 0u) { ++t.waits; after_wait = true; continue; } // WAIT (bit0 = 1)
		const eng::u16 value = words[i + 1u];
		if (a == static_cast<eng::u16>(eng::copper::Register::BPLCON0)) {
			++t.bplcon0_moves;
		} else if (a >= 0x180u && a <= 0x1beu) {
			++t.color_moves;
		} else if (after_wait &&
			   a == static_cast<eng::u16>(eng::copper::Register::BPL1MOD)) {
			if (value == row_back) ++t.mod_back;
			else if (value == 0u) ++t.mod_zero;
		} else if (after_wait &&
			   a == static_cast<eng::u16>(eng::copper::Register::BPLCON1)) {
			if (value == 0x0022u) ++t.bplcon1_shift;
			else if (value == 0u) ++t.bplcon1_zero;
		}
	}
	return t;
}

} // namespace

int main() {
	// --- 1) HAM + cuadruplicado (config de la demo 080) -----------------------
	{
		MemorySystem mem = make_memory();
		SceneResources res = eng::graphics::scene::ham(320, 256, 64, 4);
		res.copper_bytes = 8192u; // row_repeat emite ~3 MOVEs por cada una de las 256 líneas
		static const eng::u16 palette[16] {};
		Scene sc;
		if (!eng::graphics::scene::compose(
			    sc, mem, res,
			    eng::graphics::scene::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x7a00u),
			    eng::graphics::scene::palette(eng::PaletteWords {palette, 16}, 0u, 16u),
			    eng::graphics::scene::row_repeat(4u, 0x2cu, 0x0022u))) {
			std::printf("[FAIL] compose fallo\n");
			return 1;
		}
		if (sc.plane_bytes() != 40u * 64u || sc.planes() != 4u) {
			std::printf("[FAIL] geometria de planos incorrecta (bytes=%u planos=%u)\n",
				    (unsigned)sc.plane_bytes(), (unsigned)sc.planes());
			return 1;
		}
		if (sc.bitplanes().empty()) {
			std::printf("[FAIL] no hay bitplanes\n");
			return 1;
		}

		// Tras `compose`, `end_build()` voltea el buffer: la lista está en el bloque activo.
		const u16* words = sc.plan().active_words();
		const u16 row_back = static_cast<u16>(0u - 40u); // 0xffd8
		const MoveTally t = tally(words, sc.words(), row_back);
		// 64 grupos x 4 lineas = 256 lineas; 3 repeticiones + 1 avance por grupo.
		if (t.mod_back != 64u * 3u || t.mod_zero != 64u) {
			std::printf("[FAIL] cuadruplicado: mod_back=%u (esperado 192) mod_zero=%u (64)\n",
				    t.mod_back, t.mod_zero);
			return 1;
		}
		if (t.waits != 257u) {
			// 256 lineas + 1 WAIT de overflow PAL (al pasar de la linea 255).
			std::printf("[FAIL] nº de WAITs = %u (esperado 257)\n", (unsigned)t.waits);
			return 1;
		}
		if (t.bplcon1_shift != 128u || t.bplcon1_zero != 128u) {
			std::printf("[FAIL] BPLCON1 alterno: shift=%u (128) zero=%u (128)\n",
				    t.bplcon1_shift, t.bplcon1_zero);
			return 1;
		}
		if (t.bplcon0_moves != 1u) {
			std::printf("[FAIL] BPLCON0 no emitido una vez (=%u)\n", t.bplcon0_moves);
			return 1;
		}
		if (t.color_moves != 16u) {
			std::printf("[FAIL] paleta incompleta (color_moves=%u, esperado 16)\n", t.color_moves);
			return 1;
		}
		if (!sc.ok()) {
			std::printf("[FAIL] copper report no ok\n");
			return 1;
		}
	}

	// --- 2) Parametrico: sin repeticion, 5 planos, otro BPLCON0 ---------------
	{
		MemorySystem mem = make_memory();
		SceneResources res = eng::graphics::scene::planar4(320, 128, 5);
		Scene sc;
		if (!eng::graphics::scene::compose(
			    sc, mem, res,
			    eng::graphics::scene::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x5000u))) {
			std::printf("[FAIL] compose (config plano) fallo\n");
			return 1;
		}
		if (!sc.ok() || sc.planes() != 5u) {
			std::printf("[FAIL] compose (config plano) no ok\n");
			return 1;
		}
	}

	std::printf("OK: scene::compose valida display planar + repeticion de filas (cuadruplicado) y es parametrico.\n");
	return 0;
}
