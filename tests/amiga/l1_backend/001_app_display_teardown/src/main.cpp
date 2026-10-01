// L1-001: App compone display y detiene DMA antes de liberar su escena.

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
	0u,
	0u,
};
}

namespace {
constexpr eng::u16 kFrames = 8u;
constexpr eng::u16 kDmaEnableMask = 0x07ffu; // Master, bitplane, copper, blitter, sprites, disk y Paula.

struct Game {
	bool background_added = false;
	bool background_materialized = false;
	void init(auto& app) {
		const auto layer = app.add_background("integration-bg", 0u,
						      eng::Box {0, 0, 96u, 64u}, 1u);
		background_added = layer.valid();
		if (layer) {
			layer->camera().reset(eng::scene::WorldRect {0u, 0u, 128u, 64u}, eng::Size2u {64u, 64u});
			layer->camera().set_scroll_x(16u);
		}
	}
	void update(auto&) {}
	void render(auto& app) {
		background_materialized = app.world_materialization_ok();
	}
};
} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_game_memory()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00010001u);
		return 0;
	}
	const eng::u32 chip_capacity = backend.memory_manager().chip().capacity();
	bool app_started = false;
	bool background_added = false;
	bool background_materialized = false;
	{
		Game game {};
		eng::App app {backend, game, backend.memory_manager()};
		eng::GameDisplay display {};
		display.width = 64u;
		display.height = 64u;
		display.color_depth = 2u;
		display.buffers = 1u;
		if (!app.set_display(display) || !app.start()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010002u);
			return 0;
		}
		app_started = true;
		app.run(kFrames);
		if (!app.shutdown_complete()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010003u);
			return 0;
		}
		if (!game.background_added || !game.background_materialized) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010004u);
			return 0;
		}
		background_added = game.background_added;
		background_materialized = game.background_materialized;
	}

	// App::~App detiene DMA antes de destruir Scene. El readback y la capacidad del pool se
	// comprueban después de salir del scope: cero canales DMA activos y todos los bloques devueltos.
	// DMACONR ($DFF002, AHRM cap. 6) informa el estado real de todos los canales DMA.
	volatile eng::u16* const dmaconr = reinterpret_cast<volatile eng::u16*>(0xdff002u);
	const eng::u16 active_dma = static_cast<eng::u16>(*dmaconr & kDmaEnableMask);
	const bool pool_reclaimed = backend.memory_manager().chip().free_bytes() == chip_capacity;
	const eng::u32 detail = (active_dma == 0u ? 1u : 0u) | (pool_reclaimed ? 2u : 0u) |
			(static_cast<eng::u32>(app_started) << 2u) |
			(static_cast<eng::u32>(background_materialized) << 3u) |
			(static_cast<eng::u32>(background_added) << 4u);
	if (detail != 31u) {
		eng::debug::mark_failed(g_eng_run_status, detail);
	} else {
		eng::debug::mark_ready(g_eng_run_status, detail);
	}
	return 0;
}
