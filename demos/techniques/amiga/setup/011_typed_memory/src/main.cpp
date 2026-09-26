// Demo piloto de la **API de memoria tipada**: reserva DMA con `MemoryManager::chip()` ->
// `Block<PlaneTag, Chip>` -> `MemView<PlaneTag, Chip>` (dirección DMA en el tipo), y un buffer de
// CPU con `fast_or_slow` (Fast RAM si la hay, si no Slow). Muestra por overlay el resultado.
#include <eng/api/api.hpp>
#include <eng/platform/amiga/backend.hpp>

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

struct TypedMemoryDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);

		// chip + slow + frame + fast (Fast RAM solo CPU).
		m_ok = backend.configure_memory({8192u, 4096u, 1024u, 16384u});

		eng::MemoryManager& mm = backend.memory_manager();
		m_mm_has_fast = mm.has_fast();

		// DMA: planos desde el banco Chip -> `Block<PlaneTag, Chip>` -> vista `ChipPlaneView`.
		m_planes = mm.chip().reserve<eng::PlaneTag>(4096u, 16u);
		const eng::ChipPlaneView planes = m_planes.mem_view();
		m_ok = m_ok && m_planes.valid() && planes.address().valid();

		// CPU: buffer de "simulacion" con `fast_or_slow` (Fast si hay, si no Slow).
		m_sim = eng::fast_or_slow<eng::MapCellsTag>(mm, 2048u, 4u);
		m_ok = m_ok && m_sim.valid();

		if (m_ok) {
			eng::debug::mark_ready(g_eng_run_status, 0x00000000u);
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00000001u);
		}
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		const eng::u16 shade = static_cast<eng::u16>(context.frame.frame_index & 0x0fu);
		backend.set_color(0, static_cast<eng::u16>((shade << 8) | (shade << 4) | shade));
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		auto& debug = backend.debug();
		debug.clear();
		debug.text(16, 16, m_ok ? "Memoria tipada: OK" : "Memoria tipada: FAIL", 0x00ffffff);
		debug.text(16, 40, "Planos DMA: Block<PlaneTag,Chip> + MemView", 0x0000ff80);
		debug.text(16, 64, m_mm_has_fast ? "Fast RAM: si" : "Fast RAM: no", 0x00ffff00);
		debug.text(16, 88, m_sim.kind == eng::MemoryKind::Fast ? "Sim buffer: Fast"
								      : "Sim buffer: Slow",
			  0x0000ffff);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_planes {};
	eng::Block<eng::MapCellsTag> m_sim {};
	bool m_ok = false;
	bool m_mm_has_fast = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	TypedMemoryDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
