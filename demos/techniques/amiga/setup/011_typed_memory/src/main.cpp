// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/setup/011_typed_memory --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/setup/011_typed_memory --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/setup/011_typed_memory --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/setup/011_typed_memory --keep-running

// Demo piloto de la **API de memoria tipada** con el arranque de alto nivel (`run_demo`)
// y `App`: reserva DMA con `MemoryManager::chip()` -> `Block<PlaneTag, Chip>` -> `MemView`
// (dirección DMA en el tipo) y un buffer de CPU con `fast_or_slow` (Fast RAM si la hay).
// `main` declara el display y el presupuesto de memoria; `run_demo` pone `SysBase`,
// `g_eng_run_status`, el arranque del display y el bucle; el HUD rota la franja inferior.
#include <eng/api/api.hpp>
#include <eng/platform/amiga/entry.hpp>

namespace {

/// Versión mostrada por el HUD; el build la inyecta `build-demo.sh`.
constexpr eng::debug::Version kVersion {1u, 0u};

/// Paleta mínima del display: el HUD escribe con tinta 1, así que COLOR01 debe ser visible
/// (blanco) sobre el fondo (COLOR00, negro); el resto no se usa.
constexpr eng::Palette32 kPalette {{
	0x000, 0xfff, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

struct TypedMemoryDemo {
	void init(auto& app) {
		eng::MemoryManager& mm = app.memory_manager();
		m_has_fast = mm.has_fast();

		// DMA: planos desde el banco Chip -> `Block<PlaneTag, Chip>` -> vista `ChipPlaneView`.
		m_planes = mm.chip().reserve<eng::PlaneTag>(4096u, 16u);
		m_ok = m_planes.valid() && m_planes.mem_view().address().valid();

		// CPU: buffer de "simulacion" con `fast_or_slow` (Fast si hay, si no Slow).
		m_sim = eng::fast_or_slow<eng::MapCellsTag>(mm, 2048u, 4u);
		m_ok = m_ok && m_sim.valid();

		if (!m_ok) {
			eng::amiga::mark_failed(eng::debug::BootFail::Memory);
		}
	}

	// Sin trabajo por frame: la demo es estática (telemetría y render no cambian).
	void update(auto&) {}

	void render(auto& app) {
		auto& d = app.debug();
		d.clear();
		d.text(16, 16, m_ok ? "Memoria tipada: OK" : "Memoria tipada: FAIL", 0x00ffffff);
		d.text(16, 40, "Planos DMA: Block<PlaneTag,Chip> + MemView", 0x0000ff80);
		d.text(16, 64, m_has_fast ? "Fast RAM: si" : "Fast RAM: no", 0x00ffff00);
		d.text(16, 88, m_sim.kind == eng::MemoryKind::Fast ? "Sim buffer: Fast"
								      : "Sim buffer: Slow",
		       0x0000ffff);
		// HUD de depuración del host: nombre + número + versión + frame + fps.
		m_host_hud.draw(app.debug(), "011 TYPED MEMORY", kVersion, app.frame(),
				eng::debug::prof_clock(), nullptr, false);
		app.present();
		if (m_ok && !m_ready) {
			m_ready = true;
			eng::amiga::mark_ready(); // memoria validada y primer frame presentado
		}
	}

	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_planes {};
	eng::Block<eng::MapCellsTag> m_sim {};
	bool m_ok = false;
	bool m_has_fast = false;
	bool m_ready = false;
	eng::debug::HostHud m_host_hud {}; ///< HUD del host (no pisa el diagnóstico de memoria)
};

} // namespace

int main() {
	eng::GameDisplay display {}; // 320x256x4 por defecto: la demo solo rotula el HUD
	display.palette = kPalette;
	TypedMemoryDemo demo {};
	// Presupuesto explícito de la demo: 64K chip (display 320x256x4 + planos de la demo),
	// 4K slow, 1K frame, 16K fast (si la máquina no tiene Fast, `fast_or_slow` cae a Slow).
	// Espera de VBlank **activa** (familia 212/DT-011): el bucle por senal pierde el
	// despertar bajo el SO vivo (y el guard 0xE212 lo rechaza); ver el doc 212.
	return eng::amiga::run_demo(demo, display, eng::MemoryConfig {65536u, 4096u, 1024u, 16384u},
				    eng::amiga::VblankWait::Active);
}
