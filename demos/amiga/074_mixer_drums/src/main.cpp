// ============================================================================
// Demo 074: "mixer drums" — caja de ritmos con 4 samples reales (st-xx).
// ============================================================================
//
// 4 voces del mixer, cada una un instrumento de percusión real de ST-xx:
//   MixCh0 bombo · MixCh1 caja · MixCh2 hi-hat · MixCh3 palmas
//
// Patrón de 16 pasos (semicorcheas de un compás). El tempo NO se marca por
// frames (el bucle va a pocos fps con el mixer) ni por cada tick del mixer
// (~49 Hz daría ~368 BPM): se avanza un paso cada `kStepTicks` ticks del mixer
// (~6 → ~122 BPM).
//
// Cada golpe usa el enfoque validado en la demo 076: se reproduce el sample en
// LOOP y se PARA tras su duración natural (en ticks). Así suena un golpe corto
// aunque el sample sea más largo que el paso (y se pueden re-disparar, cosa que
// un loop no permite sin `stop`).

#include <eng/audio/sfx_mixer.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

__asm__(".section snd_drums.MEMF_CHIP, \"aw\"\n"
	".balign 4\n"
	".globl g_kick\ng_kick:\n.incbin \"out/assets/audio/kick_mix.raw\"\n"
	".globl g_kick_end\ng_kick_end:\n"
	".globl g_snare\ng_snare:\n.incbin \"out/assets/audio/snare_mix.raw\"\n"
	".globl g_snare_end\ng_snare_end:\n"
	".globl g_hihat\ng_hihat:\n.incbin \"out/assets/audio/hihat_mix.raw\"\n"
	".globl g_hihat_end\ng_hihat_end:\n"
	".globl g_claps\ng_claps:\n.incbin \"out/assets/audio/claps_mix.raw\"\n"
	".globl g_claps_end\ng_claps_end:\n"
	".balign 4\n");
extern "C" const eng::u8 g_kick[], g_kick_end[];
extern "C" const eng::u8 g_snare[], g_snare_end[];
extern "C" const eng::u8 g_hihat[], g_hihat_end[];
extern "C" const eng::u8 g_claps[], g_claps_end[];

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

constexpr eng::u16 kStepTicks = 6;   // ~122 BPM (semicorcheas) a ~49 Hz
constexpr eng::u16 kTickHz = 49;     // aprox., para la duración de cada golpe
constexpr eng::u32 kSampleRate = 11025;

struct DrumsDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u });
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00007401u); return; }

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007402u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00007403u); return; }
		backend.takeover_display(m_copper_ptr);

		if (!m_sfx.init(backend.memory())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00007404u);
			return;
		}
		m_sfx.set_master_volume(64);

		m_data[0] = g_kick;  m_len[0] = static_cast<eng::u32>(g_kick_end - g_kick);
		m_data[1] = g_snare; m_len[1] = static_cast<eng::u32>(g_snare_end - g_snare);
		m_data[2] = g_hihat; m_len[2] = static_cast<eng::u32>(g_hihat_end - g_hihat);
		m_data[3] = g_claps; m_len[3] = static_cast<eng::u32>(g_claps_end - g_claps);
		for (eng::u32 k = 0; k < 4u; ++k) {
			// Duración natural del sample en ticks del mixer (mínimo 1).
			eng::u32 g = (m_len[k] * kTickHz) / kSampleRate;
			m_gate_len[k] = static_cast<eng::u16>(g == 0u ? 1u : g);
		}

		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok) {
			return;
		}
		const eng::u16 now = m_sfx.counter();
		eng::u16 elapsed = static_cast<eng::u16>(now - m_last);
		if (elapsed > 32u) elapsed = 32u;
		for (eng::u16 i = 0; i < elapsed; ++i) {
			// Avanza un paso cada kStepTicks.
			if (++m_tick >= kStepTicks) {
				m_tick = 0;
				const eng::u32 s = m_step % 16u;
				// Bombo a negras (4x4), caja en 2 y 4, hi-hat a corcheas, palmas
				// con la caja: los golpes coinciden y se solapan.
				if ((s & 3u) == 0u)      hit(0, eng::audio::MixCh0); // 0,4,8,12
				if (s == 4u || s == 12u) hit(1, eng::audio::MixCh1);
				if ((s & 1u) == 0u)      hit(2, eng::audio::MixCh2);
				if (s == 12u)            hit(3, eng::audio::MixCh3);
				++m_step;
			}
		}
		m_last = now;

		if (!m_confirmed && context.frame.frame_index >= 60u) {
			eng::debug::mark_ready(g_eng_run_status, m_triggers);
			g_eng_run_status.frame =
				((m_count[0] & 0xffu) << 24u) | ((m_count[1] & 0xffu) << 16u) |
				((m_count[2] & 0xffu) << 8u) | (m_count[3] & 0xffu);
			m_confirmed = true;
		}
		(void)backend;
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		draw_scope();
		if (build_copper()) backend.install_copper_list(m_copper_ptr);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void hit(eng::u32 k, eng::u16 channel) {
		// One-shot: cada sample suena su duración natural (no se loopea, que
		// sonaría a "ametralladora" porque dura ~25-50 ms).
		eng::audio::SfxSample s { eng::Span<const eng::u8>(m_data[k], m_len[k]) };
		const eng::audio::SfxChannel ch = m_sfx.play_on(channel, s, 1, eng::audio::LoopMode::Once);
		if (ch >= 0) {
			++m_triggers;
			++m_count[k];
		}
	}

	void draw_scope() {
		const eng::u8* buf = m_sfx.buffer();
		const eng::u32 n = m_sfx.buffer_bytes();
		for (eng::u32 i = 0; i < kPlaneBytes; ++i) m_bitplanes[i] = 0;
		if (n == 0u) return;
		for (eng::u16 x = 0; x < 320u; ++x) {
			const eng::u32 si = (static_cast<eng::u32>(x) * n) / 320u;
			const eng::u16 y = static_cast<eng::u16>(255u - buf[si]);
			set_pixel(x, y);
			if (y + 1u < 256u) set_pixel(x, static_cast<eng::u16>(y + 1u));
		}
		for (eng::u16 x = 0; x < 320u; ++x) set_pixel(x, 127u);
	}

	void set_pixel(eng::u16 x, eng::u16 y) {
		if (x >= 320u || y >= 256u) return;
		m_bitplanes[static_cast<eng::u32>(y) * kBytesPerRow + (x >> 3u)]
			|= static_cast<eng::u8>(1u << (7u - (x & 7u)));
	}

	bool build_copper() {
		eng::copper::Scheduler sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplanes, kPlaneBytes
		);
		for (eng::u8 i = 0; i < 32; ++i) {
			sched.move(eng::copper::color_register(i), 0x0000);
		}
		sched.move(eng::copper::color_register(0), 0x000fu);
		sched.move(eng::copper::color_register(1), 0x0fffu);
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
	eng::u32 m_triggers = 0;
	eng::u32 m_step = 0;
	eng::u32 m_count[4] = { 0, 0, 0, 0 };
	eng::u16 m_tick = 0;
	eng::u16 m_last = 0;
	eng::u16 m_copper_words = 0;
	bool m_on[4] = { false, false, false, false };
	eng::u16 m_gate[4] = { 0, 0, 0, 0 };
	eng::u16 m_gate_len[4] = { 1, 1, 1, 1 };
	eng::audio::SfxChannel m_ch[4] = { -1, -1, -1, -1 };
	const eng::u8* m_data[4] = { nullptr, nullptr, nullptr, nullptr };
	eng::u32 m_len[4] = { 0, 0, 0, 0 };
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::audio::SfxMixer m_sfx {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DrumsDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
