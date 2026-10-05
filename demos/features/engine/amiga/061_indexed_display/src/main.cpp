// Demo — `eng::IndexedDisplay`: framebuffer indexado de nivel A (chunky -> planar).
// ----------------------------------------------------------------------------
// Tutorial: el juego **escribe índices** (1 byte/píxel, 0..15) en un buffer lineal y llama
// `fb.present()`; el engine hace el **C2P por Blitter** a los bitplanes y lo publica en VBlank.
// El juego **no ve** planos (`BPLxPT`), `Scene`, módulos ni Copper. Es el camino del emulador NES
// (`docs/engine/NES_CONSUMER_GUIDE.md` §1.2): resolución 256x240, 4 planos (16 colores), doble buffer.
//
//   bash ./tools/build/build-demo.sh demos/features/engine/amiga/061_indexed_display --debug
//   bash ./tools/run/run-demo.sh demos/features/engine/amiga/061_indexed_display --warp

#include <eng/api/api.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

// Resolución NES (256x240) y paleta de 16 colores RGB444.
constexpr eng::u16 kW = 256u;
constexpr eng::u16 kH = 240u;
constexpr eng::u16 kColors[16] = {
	0x000, 0x012, 0x024, 0x036, 0x060, 0x080, 0x0a0, 0x0c0,
	0x900, 0xb00, 0xd00, 0xf30, 0xff0, 0xfff, 0x888, 0x555,
};

struct FramebufferDemo {
	eng::IndexedDisplay<4u, 2u> m_fb {}; // 4 planos (16 colores), doble buffer
	eng::u16 m_phase = 0u;

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// Chip: 2 buffers de bitplanes (~60 KB) + copperlist doble (~16 KB) + 2 chunky 256x240
		// (~120 KB) ≈ 196 KB -> 256 KB con margen.
		if (!backend.configure_memory({256u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006101u);
			return;
		}
		// `row_repeat = 2`: el framebuffer chunky es de 256x120 y el Copper muestra cada fila
		// dos veces -> 256x240 en pantalla, con la **mitad** de fill por frame (truco 061/080).
		// (El camino de 5 planos / 32 colores esta soportado por `IndexedDisplay`, pero su
		// display aun no esta verificado en hardware; ver el README.)
		if (!m_fb.init(backend, kW, kH, eng::PaletteWords {kColors, 16u}, 16u, 2u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006102u);
			return;
		}
		eng::debug::mark_ready(g_eng_run_status, 0x06100000u);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		// Escribe índices en el framebuffer trasero: barras horizontales que ciclan de color + una
		// barra vertical que barre la pantalla. Se escribe de 4 en 4 bytes (u32) por velocidad.
		eng::Span<eng::u8> px = m_fb.framebuffer();
		auto* row32 = reinterpret_cast<eng::u32*>(px.data());
		const eng::u32 words_per_row = kW / 4u;
		for (eng::u32 y = 0u; y < m_fb.rows(); ++y) {
			const eng::u8 c = static_cast<eng::u8>(((y >> 2u) + m_phase) & 0x0fu); // 16 colores
			const eng::u32 fill = static_cast<eng::u32>(c) * 0x01010101u;
			eng::u32* row = row32 + y * words_per_row;
			for (eng::u32 i = 0u; i < words_per_row; ++i) {
				row[i] = fill;
			}
			// Barra vertical blanca (dos words = 8 px) que barre la pantalla.
			const eng::u32 bw = (static_cast<eng::u32>(m_phase) * 2u) & 0x3eu;
			row[bw] = 0x0f0f0f0fu;
			row[bw + 1u] = 0x0f0f0f0fu;
		}
		m_phase = static_cast<eng::u16>((m_phase + 1u) & 0x1fu);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		// Publica: C2P del buffer trasero a los planos + swap de copperlist en VBlank.
		m_fb.present();
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	FramebufferDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffffu);
	return 0;
}
