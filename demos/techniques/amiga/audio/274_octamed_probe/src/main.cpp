// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/audio/274_octamed_probe --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/audio/274_octamed_probe --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/audio/274_octamed_probe --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/audio/274_octamed_probe --keep-running

// ============================================================================
// Demo 274: A1 — repro del cuelgue de `_startmusic` (OctaMED).
// ============================================================================
//
// Arranca el modulo OctaMED incrustado con el modo `AudioMode::TitleOctaMED`:
// `AudioSystem::play_music(..., MusicFormat::OctaMED)` emite `jsr _startmusic`.
// Si `_startmusic` **vuelve**, se marca READY con `detail = 0x00027401`; si se cuelga
// (o crashea), el runner reporta `side_channel_unavailable` -> es la repro del bloqueo
// documentado en docs/debugging/investigaciones/octamed-startmusic-hang.md.
//
// Build (opt-in del playroutine MED + modulo concreto + frame de READY):
//   EXTRA_DEFINES='-DENG_AUDIO_OCTAMED -DMED_MODULE_FILE="mammagamma.med" -DOCTAMED_READY_FRAME=0' \
//     bash tools/build/build-demo.sh demos/techniques/amiga/audio/274_octamed_probe --debug
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/audio/audio_system.hpp>
#include <eng/graphics/copper/scheduler.hpp>
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

/// Frame en el que se marca READY. `0` = justo tras `_startmusic` (arranque); `N` = espera N
/// frames y **exige** que el playroutine haya encendido el DMA de audio (AUD0..3EN en DMACONR).
/// Se elige con `EXTRA_DEFINES=... -DOCTAMED_READY_FRAME=<n>`.
#ifndef OCTAMED_READY_FRAME
#define OCTAMED_READY_FRAME 0
#endif
constexpr eng::u32 kReadyFrame = static_cast<eng::u32>(OCTAMED_READY_FRAME);

struct OctaMedProbe {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
#if !defined(ENG_AUDIO_OCTAMED) || !defined(MED_MODULE_NUM)
		// El reproductor MED es **opt-in**: sin `-DENG_AUDIO_OCTAMED -DMED_MODULE_NUM=<n>` no se
		// compila (`OctaMedPlayer`/`play_music(OctaMED)` ni `_startmusic`), asi que la demo
		// marca READY sin ejercitar nada. Con el opt-in, reproduce/diagnostica A1.
		eng::debug::mark_ready(g_eng_run_status, 0x00027400u);
		return;
#endif
		if (!backend.configure_memory({96u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027401u);
			return;
		}
		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kPlaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(1024, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027402u);
			return;
		}
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027403u);
			return;
		}
		backend.takeover_display(m_copper_ptr);

		// Diagnostico: `_Wait1line` del playroutine gira hasta que CAMBIA el byte bajo de
		// VHPOSR (`$dff007`, contador H). Si aqui no cambia, ese bucle es un cuelgue.
		{
			volatile eng::u8* const vhpos_lo =
				reinterpret_cast<volatile eng::u8*>(0xdff007u);
			const eng::u8 first = *vhpos_lo;
			eng::u32 spins = 0u;
			while (*vhpos_lo == first && spins < 2000000u) {
				++spins;
			}
			if (spins >= 2000000u) {
				eng::debug::mark_failed(g_eng_run_status, 0x00027407u);
				return;
			}
		}

		// Modo TitleOctaMED: los 4 canales HW para el playroutine (8 voces SW).
		eng::audio::AudioConfig cfg {};
		cfg.mode = eng::audio::AudioMode::TitleOctaMED;
		if (!m_audio.init(backend.memory(), cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027404u);
			return;
		}

		// Punto de no retorno: `play_music(.., OctaMED)` -> `jsr _startmusic`. Si el
		// playroutine cuelga, NUNCA se llega a la linea siguiente.
		const bool ok =
			m_audio.play_music(eng::audio::MusicModule {}, eng::audio::MusicFormat::OctaMED);
		if (!ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027405u);
			return;
		}
		m_ok = true;
		if (kReadyFrame == 0u) {
			eng::debug::mark_ready(g_eng_run_status, 0x00027401u); // `_startmusic` retorno
		}
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		m_audio.update_music(); // el playroutine MED es frame-driven
		if (!m_ok || kReadyFrame == 0u) {
			return;
		}
		// A `kReadyFrame` comprueba que el playroutine esta **reproduciendo**: enciende el DMA
		// de audio de Paula (DMACONR bits 0..3 = AUD0..3EN). Si estan a 1, engancho su timing.
		if (context.frame.frame_index == kReadyFrame) {
			const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
			const bool playing = (dmaconr & 0x000fu) == 0x000fu;
			eng::debug::mark_ready(g_eng_run_status,
					       (static_cast<eng::u32>(dmaconr) << 16u) |
						       (playing ? 0x00027401u : 0x00027406u));
		}
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	bool build_copper() {
		eng::copper::SchedulerT<false> sched {m_copper_block};
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x1200, 1u,
					  m_bitplane_block.view, kPlaneBytes);
		sched.move(eng::copper::color_register(0), 0x002u);
		sched.move(eng::copper::color_register(1), 0x00Fu);
		sched.end();
		m_copper_ptr = sched.data();
		return sched.ok();
	}

	bool m_ok = false;
	const eng::u16* m_copper_ptr = nullptr;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::audio::AudioSystem m_audio {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	OctaMedProbe game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
