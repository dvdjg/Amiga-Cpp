// Demo piloto de la **API de memoria tipada** usando el arranque de alto nivel (`ENG_APP_MAIN`)
// y `App`: reserva DMA con `MemoryManager::chip()` -> `Block<PlaneTag, Chip>` -> `MemView`
// (dirección DMA en el tipo) y un buffer de CPU con `fast_or_slow` (Fast RAM si la hay).
// No hay `main`, `SysBase` ni `g_eng_run_status` en el codigo: los pone `ENG_APP_MAIN`.
#include <eng/api/api.hpp>
#include <eng/platform/amiga/entry.hpp>

namespace {

struct TypedMemoryDemo {
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);

		m_ok = app.configure_memory({8192u, 4096u, 1024u, 16384u});

		eng::MemoryManager& mm = app.memory_manager();
		m_has_fast = mm.has_fast();

		// DMA: planos desde el banco Chip -> `Block<PlaneTag, Chip>` -> vista `ChipPlaneView`.
		m_planes = mm.chip().reserve<eng::PlaneTag>(4096u, 16u);
		m_ok = m_ok && m_planes.valid() && m_planes.mem_view().address().valid();

		// CPU: buffer de "simulacion" con `fast_or_slow` (Fast si hay, si no Slow).
		m_sim = eng::fast_or_slow<eng::MapCellsTag>(mm, 2048u, 4u);
		m_ok = m_ok && m_sim.valid();

		if (m_ok) {
			eng::debug::mark_ready(g_eng_run_status, 0x00000000u);
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00000001u);
		}
	}

	void update(auto& app) { eng::debug::mark_frame(g_eng_run_status, app.frame()); }

	void render(auto& app) {
		auto& d = app.debug();
		d.clear();
		d.text(16, 16, m_ok ? "Memoria tipada: OK" : "Memoria tipada: FAIL", 0x00ffffff);
		d.text(16, 40, "Planos DMA: Block<PlaneTag,Chip> + MemView", 0x0000ff80);
		d.text(16, 64, m_has_fast ? "Fast RAM: si" : "Fast RAM: no", 0x00ffff00);
		d.text(16, 88, m_sim.kind == eng::MemoryKind::Fast ? "Sim buffer: Fast"
								      : "Sim buffer: Slow",
		       0x0000ffff);
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}

	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_planes {};
	eng::Block<eng::MapCellsTag> m_sim {};
	bool m_ok = false;
	bool m_has_fast = false;
};

} // namespace

ENG_APP_MAIN(TypedMemoryDemo);
