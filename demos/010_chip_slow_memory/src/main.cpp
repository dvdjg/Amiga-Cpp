#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
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

struct HexBuffer {
	char text[11] = {'0', 'x', '0', '0', '0', '0', '0', '0', '0', '0', 0};
};

constexpr char hex_digit(eng::u8 value) {
	return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('A' + value - 10);
}

HexBuffer hex32(eng::u32 value) {
	HexBuffer out {};
	for (eng::u8 i = 0; i < 8; ++i) {
		const eng::u8 shift = static_cast<eng::u8>((7 - i) * 4);
		out.text[2 + i] = hex_digit(static_cast<eng::u8>((value >> shift) & 0x0f));
	}
	return out;
}

eng::s16 bar_width(eng::u32 used, eng::u32 capacity, eng::s16 max_width) {
	if (capacity == 0) {
		return 0;
	}
	const eng::u32 scaled = (used * static_cast<eng::u32>(max_width)) / capacity;
	return static_cast<eng::s16>(scaled);
}

void draw_bar(eng::amiga::DebugOverlay& debug, eng::s16 x, eng::s16 y, eng::s16 width, eng::s16 height, eng::u32 used, eng::u32 capacity, eng::u32 color) {
	debug.rect(x, y, static_cast<eng::s16>(x + width), static_cast<eng::s16>(y + height), 0x00ffffff);
	const eng::s16 fill = bar_width(used, capacity, static_cast<eng::s16>(width - 2));
	if (fill > 0) {
		debug.filled_rect(static_cast<eng::s16>(x + 1), static_cast<eng::s16>(y + 1), static_cast<eng::s16>(x + 1 + fill), static_cast<eng::s16>(y + height - 1), color);
	}
}

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
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
			eng::debug::mark_ready(g_eng_run_status, 0x00000010u);
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00000011u);
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		const eng::u16 pulse = static_cast<eng::u16>((context.frame.frame_index >> 2) & 0x0f);
		backend.set_color(0, static_cast<eng::u16>((pulse << 8) | 0x002));
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
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

		const HexBuffer chip_base = hex32(static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(memory.chip.base())));
		const HexBuffer slow_base = hex32(static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(memory.slow.base())));
		debug.text(76, 314, "Chip base:", 0x00ffffff);
		debug.text(196, 314, chip_base.text, 0x000080ff);
		debug.text(76, 342, "Slow base:", 0x00ffffff);
		debug.text(196, 342, slow_base.text, 0x0000ff80);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	void log_memory(eng::amiga::MinimalBackend& backend) {
		const auto& memory = backend.memory();
		KPrintF(
			"AMG010 chip base=%lx used=%ld cap=%ld slow base=%lx used=%ld cap=%ld frame used=%ld cap=%ld ok=%ld\n",
			static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(memory.chip.base())),
			memory.chip.used(),
			memory.chip.capacity(),
			static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(memory.slow.base())),
			memory.slow.used(),
			memory.slow.capacity(),
			memory.frame.used(),
			memory.frame.capacity(),
			m_memory_ok ? 1L : 0L
		);
	}

	eng::MemoryBlock m_copper_template {};
	eng::MemoryBlock m_bitplane_budget {};
	eng::MemoryBlock m_entity_pool {};
	eng::MemoryBlock m_script_blob {};
	eng::MemoryBlock m_frame_jobs {};
	eng::MemoryBlock m_expected_fail {};
	bool m_memory_ok = false;
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
