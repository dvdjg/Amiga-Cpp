#include <amg/engine.hpp>
#include <amg/debug/run_status.hpp>
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

struct HexBuffer {
	char text[11] = {'0', 'x', '0', '0', '0', '0', '0', '0', '0', '0', 0};
};

constexpr char hex_digit(amg::u8 value) {
	return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('A' + value - 10);
}

HexBuffer hex32(amg::u32 value) {
	HexBuffer out {};
	for (amg::u8 i = 0; i < 8; ++i) {
		const amg::u8 shift = static_cast<amg::u8>((7 - i) * 4);
		out.text[2 + i] = hex_digit(static_cast<amg::u8>((value >> shift) & 0x0f));
	}
	return out;
}

amg::s16 bar_width(amg::u32 used, amg::u32 capacity, amg::s16 max_width) {
	if (capacity == 0) {
		return 0;
	}
	const amg::u32 scaled = (used * static_cast<amg::u32>(max_width)) / capacity;
	return static_cast<amg::s16>(scaled);
}

void draw_bar(amg::amiga::DebugOverlay& debug, amg::s16 x, amg::s16 y, amg::s16 width, amg::s16 height, amg::u32 used, amg::u32 capacity, amg::u32 color) {
	debug.rect(x, y, static_cast<amg::s16>(x + width), static_cast<amg::s16>(y + height), 0x00ffffff);
	const amg::s16 fill = bar_width(used, capacity, static_cast<amg::s16>(width - 2));
	if (fill > 0) {
		debug.filled_rect(static_cast<amg::s16>(x + 1), static_cast<amg::s16>(y + 1), static_cast<amg::s16>(x + 1 + fill), static_cast<amg::s16>(y + height - 1), color);
	}
}

struct DemoGame {
	void init(amg::amiga::MinimalBackend& backend, amg::GameContext&) {
		amg::debug::mark_init_started(g_amg_run_status);
		m_memory_ok = backend.configure_memory({
			32u * 1024u,
			32u * 1024u,
			8u * 1024u,
		});

		m_copper_template = backend.memory().chip.allocate(1024, 16);
		m_bitplane_budget = backend.memory().chip.allocate(12 * 1024, 64);
		m_entity_pool = backend.memory().slow.allocate(4096, 16);
		m_script_blob = backend.memory().slow.allocate(8192, 16);
		m_frame_jobs = backend.memory().frame.allocate(2048, 16);
		m_expected_fail = backend.memory().chip.allocate(64 * 1024, 16);

		m_memory_ok = m_memory_ok
			&& m_copper_template.valid()
			&& m_bitplane_budget.valid()
			&& m_entity_pool.valid()
			&& m_script_blob.valid()
			&& m_frame_jobs.valid()
			&& !m_expected_fail.valid();

		log_memory(backend);
		if (m_memory_ok) {
			amg::debug::mark_ready(g_amg_run_status, 0x00000010u);
		} else {
			amg::debug::mark_failed(g_amg_run_status, 0x00000011u);
		}
	}

	void update(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		amg::debug::mark_frame(g_amg_run_status, context.frame.frame_index);
		const amg::u16 pulse = static_cast<amg::u16>((context.frame.frame_index >> 2) & 0x0f);
		backend.set_color(0, static_cast<amg::u16>((pulse << 8) | 0x002));
	}

	void render(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		auto& debug = backend.debug();
		const auto& memory = backend.memory();

		debug.clear();
		debug.filled_rect(48, 70, 700, 286, m_memory_ok ? 0x00082030 : 0x00502020);
		debug.rect(48, 70, 700, 286, 0x00ffffff);
		debug.text(76, 96, "AMG demo 010 - Chip/Slow memory model", 0x00ffffff);
		debug.text(76, 128, "Profile: A500_1MB_Slow (Slow is capacity, not true Fast)", 0x0000ff80);
		debug.text(76, 160, m_memory_ok ? "Arena checks: OK" : "Arena checks: FAIL", 0x00ffff00);

		draw_bar(debug, 76, 194, 460, 22, memory.chip.used(), memory.chip.capacity(), 0x000080ff);
		debug.text(552, 198, "Chip DMA", 0x00ffffff);

		draw_bar(debug, 76, 226, 460, 22, memory.slow.used(), memory.slow.capacity(), 0x0000ff80);
		debug.text(552, 230, "Slow metadata", 0x00ffffff);

		draw_bar(debug, 76, 258, 460, 22, memory.frame.used(), memory.frame.capacity(), 0x00ffff00);
		debug.text(552, 262, "Frame scratch", 0x00ffffff);

		const HexBuffer chip_base = hex32(static_cast<amg::u32>(reinterpret_cast<amg::uintptr>(memory.chip.base())));
		const HexBuffer slow_base = hex32(static_cast<amg::u32>(reinterpret_cast<amg::uintptr>(memory.slow.base())));
		debug.text(76, 314, "Chip base:", 0x00ffffff);
		debug.text(196, 314, chip_base.text, 0x000080ff);
		debug.text(76, 342, "Slow base:", 0x00ffffff);
		debug.text(196, 342, slow_base.text, 0x0000ff80);
		amg::debug::probe_when_ready(g_amg_run_status, context.frame.frame_index);
	}

	void log_memory(amg::amiga::MinimalBackend& backend) {
		const auto& memory = backend.memory();
		KPrintF(
			"AMG010 chip base=%lx used=%ld cap=%ld slow base=%lx used=%ld cap=%ld frame used=%ld cap=%ld ok=%ld\n",
			static_cast<amg::u32>(reinterpret_cast<amg::uintptr>(memory.chip.base())),
			memory.chip.used(),
			memory.chip.capacity(),
			static_cast<amg::u32>(reinterpret_cast<amg::uintptr>(memory.slow.base())),
			memory.slow.used(),
			memory.slow.capacity(),
			memory.frame.used(),
			memory.frame.capacity(),
			m_memory_ok ? 1L : 0L
		);
	}

	amg::MemoryBlock m_copper_template {};
	amg::MemoryBlock m_bitplane_budget {};
	amg::MemoryBlock m_entity_pool {};
	amg::MemoryBlock m_script_blob {};
	amg::MemoryBlock m_frame_jobs {};
	amg::MemoryBlock m_expected_fail {};
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
