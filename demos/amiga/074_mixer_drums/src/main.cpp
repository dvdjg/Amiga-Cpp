// ============================================================================
// Demo 074: "mixer drums" — caja de ritmos con 4 samples reales (st-xx).
// ============================================================================
//
// Hito de la app de audio: 4 voces del mixer, cada una un instrumento de
// percusión real de los packs ST-xx, disparado con su propio patrón rítmico.
//   - MixCh0: bombo  (ST-01/BassDrum1)
//   - MixCh1: caja   (ST-01/Snare1)
//   - MixCh2: hi-hat (ST-01/CloseHiHat)
//   - MixCh3: palmas (ST-01/Claps1)
//
// Los samples se convierten a 8-bit con signo y se remuestrean 44100 -> 11025
// (el mixer sale a 11 kHz), a múltiplo de 4. Patrón de 32 frames (~0.64 s a 50 Hz).
//
// Evidencia: nº de disparos y buffer del mixer (pico/valle/cruces).

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

// 4 samples de percusión reales, incrustados en Chip RAM.
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

constexpr eng::u32 kPatternFrames = 32; // ~0.64 s a 50 Hz

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

		// Precarga los 4 instrumentos (canal fijo cada uno; se re-disparan).
		m_kick_len = static_cast<eng::u32>(g_kick_end - g_kick);
		m_snare_len = static_cast<eng::u32>(g_snare_end - g_snare);
		m_hihat_len = static_cast<eng::u32>(g_hihat_end - g_hihat);
		m_claps_len = static_cast<eng::u32>(g_claps_end - g_claps);

		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok) {
			return;
		}
		// El bucle principal puede ir a pocos fps con el mixer activo, así que el
		// ritmo NO se marca por frames sino por las interrupciones del mixer
		// (~49 Hz, su reloj de audio). Se procesan los pasos transcurridos.
		const eng::u16 now = m_sfx.counter();
		eng::u16 elapsed = static_cast<eng::u16>(now - m_last);
		if (elapsed > 16u) elapsed = 16u;
		for (eng::u16 i = 0; i < elapsed; ++i) {
			const eng::u32 s = m_step % 16u; // 16 pasos (semicorcheas de un compás)
			if (s == 0u || s == 8u)  trigger(g_kick, m_kick_len, eng::audio::MixCh0);
			if (s == 4u || s == 12u) trigger(g_snare, m_snare_len, eng::audio::MixCh1);
			if ((s & 1u) == 0u)      trigger(g_hihat, m_hihat_len, eng::audio::MixCh2);
			if (s == 14u)            trigger(g_claps, m_claps_len, eng::audio::MixCh3);
			++m_step;
		}
		m_last = now;

		if (!m_confirmed && context.frame.frame_index >= 60u) {
			const eng::u8* buf = m_sfx.buffer();
			const eng::u32 n = m_sfx.buffer_bytes();
			eng::u8 vmin = 0xffu, vmax = 0u;
			eng::u32 changes = 0;
			for (eng::u32 i = 0; i < n; ++i) {
				const eng::u8 v = buf[i];
				if (v < vmin) vmin = v;
				if (v > vmax) vmax = v;
				if (i > 0u && ((buf[i - 1u] ^ v) & 0x80u) != 0u) ++changes;
			}
			eng::debug::mark_ready(g_eng_run_status, m_mask);
			g_eng_run_status.frame = (static_cast<eng::u32>(vmax) << 24u) |
				(static_cast<eng::u32>(vmin) << 16u) | (changes & 0xffffu);
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
	void trigger(const eng::u8* data, eng::u32 len, eng::u16 channel) {
		eng::audio::SfxSample s { eng::Span<const eng::u8>(data, len) };
		if (m_sfx.play_on(channel, s, 1, eng::audio::LoopMode::Once) >= 0) {
			++m_triggers;
			m_mask |= static_cast<eng::u8>(1u << (channel >> 5u)); // 16->0,32->1,64->2,128->3
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
	eng::u16 m_last = 0;
	eng::u8 m_mask = 0;
	eng::u16 m_copper_words = 0;
	eng::u32 m_kick_len = 0;
	eng::u32 m_snare_len = 0;
	eng::u32 m_hihat_len = 0;
	eng::u32 m_claps_len = 0;
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
	engine.run_frames(0xffff);

	return 0;
}
