// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/audio/216_audio_modes --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/audio/216_audio_modes
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/audio/216_audio_modes --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/audio/216_audio_modes

// ============================================================================
// Demo 216: modos de audio y eventos (A0/A2) sin tracker.
// ============================================================================
//
// Ejercita en hardware la superficie de A0/A2 con SFX (sin módulo de tracker):
//   - `AudioSystem::init(memory, cfg)` con `AudioMode::GameSfxOnly` y `set_mode(Silent)` (A0);
//   - `tick_frame(port)` + `notify_underrun()` -> `MsgType::AudioUnderrun` una vez (A2).
//
// Evidencia: `mark_ready` cuando el port recibio el `AudioUnderrun` tras `notify_underrun()`;
// el `detail` guarda DMACONR. Ver docs/engine/architecture/GAME_AUDIO.md §7-8.
// ============================================================================

#include <eng/audio/audio_system.hpp>
#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/os/port.hpp>
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
	0,
	0,
};
}

namespace {

constexpr eng::u16 kBytesPerRow = 40u;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBeepLen = 128u;

constexpr eng::u32 kFrameSilent = 60u;
constexpr eng::u32 kFrameUnderrun = 80u;
constexpr eng::u32 kFrameConfirm = 100u;

struct AudioModesDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021601u);
			return;
		}
		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kPlaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(1024, 16);
		m_beep_block = backend.memory().chip.allocate_block<eng::AudioTag>(kBeepLen, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_beep_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021602u);
			return;
		}
		gen_square(m_beep_block.view.data(), kBeepLen, 4);
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021603u);
			return;
		}
		backend.takeover_display(m_copper_ptr);

		// A0: modo GameSfxOnly (los 4 canales para el mixer) + un SFX.
		eng::audio::AudioConfig cfg {};
		cfg.mode = eng::audio::AudioMode::GameSfxOnly;
		if (!m_audio.init(backend.memory(), cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021604u);
			return;
		}
		m_audio.play_sfx(beep_sample(), 1, eng::audio::LoopMode::Loop);
		m_init_ok = true;
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		if (!m_init_ok) {
			return;
		}
		// A2: tick de frame -> postea MusicEnd/AudioUnderrun si toca (una vez por evento).
		m_audio.tick_frame(m_port);
		drain();

		const eng::u32 f = context.frame.frame_index;
		if (f == kFrameSilent) {
			m_audio.set_mode(eng::audio::AudioMode::Silent); // A0: corte
		} else if (f == kFrameUnderrun) {
			m_audio.notify_underrun(); // A2: evento -> AudioUnderrun una vez
		} else if (f == kFrameConfirm) {
			const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
			if (m_underrun_seen) {
				eng::debug::mark_ready(g_eng_run_status,
						       (static_cast<eng::u32>(dmaconr) << 16u) | 1u);
			} else {
				eng::debug::mark_failed(g_eng_run_status, 0x00021605u);
			}
		}
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	eng::audio::SfxSample beep_sample() {
		return { eng::Span<const eng::u8>(m_beep_block.view.as_const().data(), kBeepLen) };
	}
	void gen_square(eng::u8* dst, eng::u32 len, eng::u32 half) {
		for (eng::u32 i = 0; i < len; ++i) {
			dst[i] = ((i / half) & 1u) ? 24u : static_cast<eng::u8>(256u - 24u);
		}
	}
	void drain() {
		eng::os::Msg m {};
		while (m_port.pop(m)) {
			if (m.type == eng::os::MsgType::AudioUnderrun) {
				m_underrun_seen = true;
			}
		}
	}
	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x1200, 1u,
					  m_bitplane_block.view, kPlaneBytes);
		sched.move(eng::copper::color_register(0), 0x001u);
		sched.move(eng::copper::color_register(1), 0x00Fu);
		sched.end();
		m_copper_ptr = sched.data();
		return sched.ok();
	}

	bool m_init_ok = false;
	bool m_underrun_seen = false;
	const eng::u16* m_copper_ptr = nullptr;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::AudioTag> m_beep_block {};
	eng::audio::AudioSystem m_audio {};
	eng::os::MsgPort<> m_port {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	AudioModesDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
