#include <amg/engine.hpp>
#include <amg/debug/run_status.hpp>
#include <amg/graphics/copper/copper.hpp>
#include <amg/platform/amiga_minimal.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile amg::debug::RunStatus g_amg_run_status {
	amg::debug::run_status_magic,
	amg::debug::run_status_version,
	static_cast<amg::u16>(amg::debug::RunState::Cold),
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
	void init(amg::amiga::MinimalBackend& backend, amg::GameContext&) {
		amg::debug::mark_init_started(g_amg_run_status);
		m_memory_ok = backend.configure_memory({
			8u * 1024u,  // Chip: copperlist y futuros recursos DMA pequenos.
			4u * 1024u,  // Slow: metadatos de la demo.
			2u * 1024u,  // Frame scratch.
		});

		const amg::MemoryBlock copper_memory = backend.memory().chip.allocate(512, 16);
		amg::copper::ListBuilder copper { copper_memory };

		// Esta demo no usa bitplanes. Limpiamos BPL DMA desde la propia copperlist
		// para que el display de AmigaDOS no pueda seguir componiendo encima.
		copper.move(amg::copper::Register::DMACON, amg::copper::DmaBitplane);
		copper.move(
			amg::copper::Register::DMACON,
			static_cast<amg::u16>(amg::copper::DmaSetClear | amg::copper::DmaMaster | amg::copper::DmaCopper)
		);

		// Display basico lowres sin bitplanes.
		//
		// BPLCON0 bit 9 mantiene salida color; los bits 12-14 quedan a 0 porque no
		// habilitamos planos. Al no haber BPL DMA, COLOR00 domina el fondo.
		copper.move(amg::copper::Register::BPLCON0, 0x0200);
		copper.move(amg::copper::Register::BPLCON1, 0x0000);
		copper.move(amg::copper::Register::BPLCON2, 0x0000);

		// Ventana visible clasica PAL lowres 320x256, tomada de los ejemplos del
		// Hardware Reference Manual y del codigo historico del workspace.
		copper.move(amg::copper::Register::DIWSTRT, 0x2c81);
		copper.move(amg::copper::Register::DIWSTOP, 0x2cc1);
		copper.move(amg::copper::Register::DDFSTRT, 0x0038);
		copper.move(amg::copper::Register::DDFSTOP, 0x00d0);

		// Bandas de color. Cada WAIT detiene el Copper hasta que el raster llega a
		// una linea aproximada, y el MOVE siguiente cambia COLOR00.
		copper.move(amg::copper::Register::COLOR00, 0x0000);
		copper.wait_line(0x30);
		copper.move(amg::copper::Register::COLOR00, 0x0f00);
		copper.wait_line(0x60);
		copper.move(amg::copper::Register::COLOR00, 0x00f0);
		copper.wait_line(0x90);
		copper.move(amg::copper::Register::COLOR00, 0x000f);
		copper.wait_line(0xc0);
		copper.move(amg::copper::Register::COLOR00, 0x0ff0);
		copper.wait_line(0xf0);
		copper.move(amg::copper::Register::COLOR00, 0x00ff);
		copper.end();

		m_copper_ok = copper.ok();
		m_copper_words = copper.words_used();
		m_copper_words_ptr = copper.data();

		if (m_memory_ok && m_copper_ok) {
			backend.install_copper_list(m_copper_words_ptr);
			amg::debug::mark_ready(g_amg_run_status, static_cast<amg::u32>(m_copper_words));
		} else {
			amg::debug::mark_failed(g_amg_run_status, 0x00000020u);
		}
	}

	void update(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		amg::debug::mark_frame(g_amg_run_status, context.frame.frame_index);
		// La demo no anima nada desde CPU. El punto es que el Copper haga el trabajo
		// de raster sin intervencion por frame.
		if (m_copper_ok) {
			backend.install_copper_list(m_copper_words_ptr);
		}
	}

	void render(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		// No usamos overlay en esta demo: la captura debe validar solo el resultado
		// de hardware. Si el overlay o AmigaDOS aparecen, el analizador debe fallar.
		if (m_copper_ok && m_copper_words > 0) {
			backend.install_copper_list(m_copper_words_ptr);
		}
		amg::debug::probe_when_ready(g_amg_run_status, context.frame.frame_index);
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	amg::u16 m_copper_words = 0;
	const amg::u16* m_copper_words_ptr = nullptr;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	amg::debug::reset(g_amg_run_status);

	amg::amiga::MinimalBackend backend {};
	DemoGame game {};
	amg::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
