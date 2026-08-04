#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

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

/// Demo de Copper minimo.
///
/// Esta demo muestra el primer paso close-to-the-metal real del engine:
/// construir una copperlist en Chip RAM, instalarla en COP1LC y dejar que el Copper
/// cambie COLOR00 sincronizado con el haz.
///
/// No hay bitplanes. La pantalla visible es simplemente el color de fondo que el
/// Copper va cambiando por bandas horizontales. El overlay de WinUAE-DBG se usa
/// solo como texto/tutorial de validacion.
struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			8u * 1024u,  // Chip: copperlist y futuros recursos DMA pequenos.
			4u * 1024u,  // Slow: metadatos de la demo.
			2u * 1024u,  // Frame scratch.
		});

		const eng::MemoryBlock copper_memory = backend.memory().chip.allocate(512, 16);
		eng::copper::ListBuilder copper { copper_memory };

		// Esta demo no usa bitplanes. Limpiamos BPL DMA desde la propia copperlist
		// para que el display de AmigaDOS no pueda seguir componiendo encima.
		copper.move(eng::copper::Register::DMACON, eng::copper::DmaBitplane);
		copper.move(
			eng::copper::Register::DMACON,
			static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster | eng::copper::DmaCopper)
		);

		// Display basico lowres sin bitplanes.
		//
		// BPLCON0 bit 9 mantiene salida color; los bits 12-14 quedan a 0 porque no
		// habilitamos planos. Al no haber BPL DMA, COLOR00 domina el fondo.
		copper.move(eng::copper::Register::BPLCON0, 0x0200);
		copper.move(eng::copper::Register::BPLCON1, 0x0000);
		copper.move(eng::copper::Register::BPLCON2, 0x0000);

		// Ventana visible clasica PAL lowres 320x256, tomada de los ejemplos del
		// Hardware Reference Manual y del codigo historico del workspace.
		copper.move(eng::copper::Register::DIWSTRT, 0x2c81);
		copper.move(eng::copper::Register::DIWSTOP, 0x2cc1);
		copper.move(eng::copper::Register::DDFSTRT, 0x0038);
		copper.move(eng::copper::Register::DDFSTOP, 0x00d0);

		// Bandas de color. Cada WAIT detiene el Copper hasta que el raster llega a
		// una linea aproximada, y el MOVE siguiente cambia COLOR00.
		copper.move(eng::copper::Register::COLOR00, 0x0000);
		copper.wait_line(0x30);
		copper.move(eng::copper::Register::COLOR00, 0x0f00);
		copper.wait_line(0x60);
		copper.move(eng::copper::Register::COLOR00, 0x00f0);
		copper.wait_line(0x90);
		copper.move(eng::copper::Register::COLOR00, 0x000f);
		copper.wait_line(0xc0);
		copper.move(eng::copper::Register::COLOR00, 0x0ff0);
		copper.wait_line(0xf0);
		copper.move(eng::copper::Register::COLOR00, 0x00ff);
		copper.end();

		m_copper_ok = copper.ok();
		m_copper_words = copper.words_used();
		m_copper_words_ptr = copper.data();

		if (m_memory_ok && m_copper_ok) {
			backend.install_copper_list(m_copper_words_ptr);
			eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_copper_words));
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00000020u);
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		// La demo no anima nada desde CPU. El punto es que el Copper haga el trabajo
		// de raster sin intervencion por frame.
		if (m_copper_ok) {
			backend.install_copper_list(m_copper_words_ptr);
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		// No usamos overlay en esta demo: la captura debe validar solo el resultado
		// de hardware. Si el overlay o AmigaDOS aparecen, el analizador debe fallar.
		if (m_copper_ok && m_copper_words > 0) {
			backend.install_copper_list(m_copper_words_ptr);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	eng::u16 m_copper_words = 0;
	const eng::u16* m_copper_words_ptr = nullptr;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
