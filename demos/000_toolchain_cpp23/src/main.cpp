#include <amg/engine.hpp>
#include <amg/debug/run_status.hpp>
#include <amg/memory/arena.hpp>
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

struct DemoGame {
	static consteval int language_level_marker() {
		return 23;
	}

	template <typename T>
	static constexpr T clamp_to_demo_range(T value) {
		if (value < static_cast<T>(0)) {
			return static_cast<T>(0);
		}
		if (value > static_cast<T>(15)) {
			return static_cast<T>(15);
		}
		return value;
	}

	void init(amg::amiga::MinimalBackend& backend, amg::GameContext&) {
		amg::debug::mark_init_started(g_amg_run_status);
		static_assert(language_level_marker() == 23);

		m_memory_ok = backend.configure_memory({4096, 4096, 1024});

		auto chip_block = backend.memory().chip.allocate(128, 16);
		auto slow_block = backend.memory().slow.allocate(128, 16);
		m_memory_ok = m_memory_ok && chip_block.valid() && slow_block.valid();
		if (m_memory_ok) {
			amg::debug::mark_ready(g_amg_run_status, 0x00000000u);
		} else {
			amg::debug::mark_failed(g_amg_run_status, 0x00000001u);
		}
	}

	void update(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		amg::debug::mark_frame(g_amg_run_status, context.frame.frame_index);
		const amg::u16 shade = clamp_to_demo_range<amg::u16>(context.frame.frame_index & 0x0f);
		const amg::u16 color = static_cast<amg::u16>((shade << 8) | (shade << 4) | shade);
		backend.set_color(0, color);
	}

	void render(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		auto& debug = backend.debug();
		debug.clear();
		debug.filled_rect(64, 80, 540, 210, m_memory_ok ? 0x00103060 : 0x00601010);
		debug.rect(64, 80, 540, 210, 0x00ffffff);
		debug.text(90, 108, "AMG demo 000 - C++23 abstractions online", 0x00ffffff);
		debug.text(90, 136, "Profile: A500_1MB_Slow", 0x0000ff80);
		debug.text(90, 164, m_memory_ok ? "Memory arenas: OK" : "Memory arenas: FAIL", 0x00ffff00);

		if ((context.frame.frame_index & 1u) == 0) {
			debug.text(90, 192, "Frame scheduler: running", 0x0000ffff);
		}
		amg::debug::probe_when_ready(g_amg_run_status, context.frame.frame_index);
	}

	bool m_memory_ok = false;
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
