// ============================================================================
// Demo 072: "sample channel" — un sample REAL (st-xx) en un canal de Paula.
// ============================================================================
//
// Primer paso de la app de audio: usar una muestra real de los packs ST-xx
// (8-bit mono, 44100 Hz) en UN canal hardware (AUD0) directamente, sin mixer ni
// música. El .raw (8-bit con signo) se genera de `ST-01/BassDrum1.wav` con
// `out/tmp/wav-to-raw.cjs` y se incrusta en Chip RAM por `incbin`.
//
// AUD0PER = 80 -> 3546895/80 ≈ 44336 Hz (ritmo natural del sample).
//
// Evidencia: `mark_ready` guarda DMACONR (bit 0 = AUD0EN) y la longitud en words.

#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

// Sample real incrustado en Chip RAM (hunk HUNKF_CHIP) desde out/assets/audio.
__asm__(".section snd_alien.MEMF_CHIP, \"aw\"\n"
	".balign 4\n"
	".globl g_sample\ng_sample:\n"
	".incbin \"out/assets/audio/alien.raw\"\n"
	".globl g_sample_end\ng_sample_end:\n"
	".balign 4\n");
extern "C" const eng::u8 g_sample[];
extern "C" const eng::u8 g_sample_end[];

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

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u16 kAudPeriod = 80; // ≈44.3 kHz

struct SampleChannelDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u });
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00007201u); return; }

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007202u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00007203u); return; }
		backend.takeover_display(m_copper_ptr);

		// Programa AUD0 para reproducir el sample real en bucle.
		const eng::u32 bytes = static_cast<eng::u32>(g_sample_end - g_sample);
		const eng::u16 words = static_cast<eng::u16>(bytes / 2u);
		const eng::u32 addr = reinterpret_cast<eng::u32>(g_sample);
		*reinterpret_cast<volatile eng::u16*>(0xdff0a0u) = static_cast<eng::u16>(addr >> 16); // AUD0LCH
		*reinterpret_cast<volatile eng::u16*>(0xdff0a2u) = static_cast<eng::u16>(addr & 0xffffu); // AUD0LCL
		*reinterpret_cast<volatile eng::u16*>(0xdff0a4u) = words;  // AUD0LEN (words)
		*reinterpret_cast<volatile eng::u16*>(0xdff0a8u) = 64;     // AUD0VOL
		*reinterpret_cast<volatile eng::u16*>(0xdff0a6u) = kAudPeriod; // AUD0PER
		*reinterpret_cast<volatile eng::u16*>(0xdff096u) = 0x8201u;    // DMACON: SET|MASTER|AUD0

		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok || m_confirmed) {
			return;
		}
		if (context.frame.frame_index >= 50u) {
			const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
			const eng::u32 bytes = static_cast<eng::u32>(g_sample_end - g_sample);
			eng::debug::mark_ready(g_eng_run_status, (static_cast<eng::u32>(dmaconr) << 16u) | (bytes / 2u));
			m_confirmed = true;
		}
		(void)backend;
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool build_copper() {
		eng::copper::Scheduler sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplanes, kPlaneBytes
		);
		for (eng::u8 i = 0; i < 32; ++i) {
			sched.move(eng::copper::color_register(i), 0x0000);
		}
		sched.wait_line(0xf8);
		sched.move(eng::copper::Register::COLOR00, 0x0000);
		sched.end();
		m_copper_ok = sched.ok();
		m_copper_words = sched.words_used();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	bool m_init_ok = false;
	bool m_confirmed = false;
	eng::u16 m_copper_words = 0;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	SampleChannelDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
