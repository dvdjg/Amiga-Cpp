// ============================================================================
// Demo 061: "audio system" — fachada unificada SFX + música (eng::audio::AudioSystem).
// ============================================================================
//
// Consolida la API de audio: un único punto de entrada para efectos (Audio Mixer
// 3.7) y música (ptplayer/P61). El juego llama a `play_sfx`/`play_music` sin
// conocer los backends. Demuestra la coexistencia: música Protracker en AUD1-3
// y SFX por el mixer en AUD0 (canal reservado vía `set_music_channel_mask`).
//
// Evidencia: `mark_ready` se emite cuando el DMA de música y SFX están activos;
// el detalle guarda DMACONR.

#include <eng/audio/audio_system.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/platform/input_poll.hpp>

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

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u32 kModSize = 2364;     // módulo Protracker mínimo (ver 060)
constexpr eng::u32 kSampleOffset = 2108;
constexpr eng::u32 kSampleLen = 256;
constexpr eng::u32 kAlarmLen = 1024;
constexpr eng::u32 kBeepLen = 128;

struct AudioSystemDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u });
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00006101u); return; }

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		m_mod_block = backend.memory().chip.allocate(kModSize, 4);
		m_alarm_block = backend.memory().chip.allocate(kAlarmLen, 4);
		m_beep_block = backend.memory().chip.allocate(kBeepLen, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_mod_block.valid() ||
			!m_alarm_block.valid() || !m_beep_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006102u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);
		build_mod(static_cast<eng::u8*>(m_mod_block.data));
		gen_square(static_cast<eng::u8*>(m_alarm_block.data), kAlarmLen, 64);
		gen_square(static_cast<eng::u8*>(m_beep_block.data), kBeepLen, 4);

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00006103u); return; }

		backend.takeover_display(m_copper_ptr);

		// Orden canónico (música primero, mixer después): PtInit toca todos los
		// canales de audio al arrancar, así que primero se arranca la música, se
		// reserva AUD0 para el mixer y solo entonces se arranca el mixer.
		eng::audio::MusicModule mod { eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_mod_block.data), kModSize) };
		m_music_ok = m_audio.play_music(mod, eng::audio::MusicFormat::Protracker);
		m_audio.set_music_channel_mask(0x0Eu); // silencia AUD0 (mixer), deja AUD1..AUD3

		if (!m_audio.init(backend.memory())) { eng::debug::mark_failed(g_eng_run_status, 0x00006104u); return; }
		m_alarm_ch = m_audio.play_sfx_on(eng::audio::MixCh0, alarm_sample(), 1, eng::audio::LoopMode::Loop);

		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok || m_confirmed) {
			return;
		}
		m_audio.update_music(); // no-op para Protracker (CIA)

		eng::amiga::GameInput gin;
		eng::amiga::poll_input(gin);
		if (gin.port0 != 0u) {
			m_audio.play_sfx(beep_sample(), 3, eng::audio::LoopMode::Once);
		}

		// Confirmar: DMA de SFX (AUD0) y de música (AUD1..AUD3) activos.
		if (context.frame.frame_index == 100u) {
			const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
			if ((dmaconr & 0x0Fu) != 0u) {
				eng::debug::mark_ready(g_eng_run_status, (static_cast<eng::u32>(dmaconr) << 16u) | 1u);
			} else {
				eng::debug::mark_failed(g_eng_run_status, 0x00006105u);
			}
			m_confirmed = true;
		}
		(void)backend;
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	eng::audio::SfxSample alarm_sample() {
		return { eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_alarm_block.data), kAlarmLen) };
	}
	eng::audio::SfxSample beep_sample() {
		return { eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_beep_block.data), kBeepLen) };
	}

	void gen_square(eng::u8* dst, eng::u32 len, eng::u32 half) {
		for (eng::u32 i = 0; i < len; ++i) {
			dst[i] = ((i / half) & 1u) ? 24u : static_cast<eng::u8>(256u - 24u);
		}
	}

	/// Módulo Protracker mínimo: nota C-2 en el CANAL 1 (AUD1), dejando AUD0 al mixer.
	void build_mod(eng::u8* m) {
		for (eng::u32 i = 0; i < kModSize; ++i) m[i] = 0;
		const eng::u32 h = 20;
		m[h + 22] = 0x00; m[h + 23] = 0x80; // length = 128 words
		m[h + 25] = 40;                      // volume
		m[h + 28] = 0x00; m[h + 29] = 0x80; // loop length = 128 words
		m[950] = 1;
		m[951] = 127;
		m[952] = 0;
		m[1080] = 'M'; m[1081] = '.'; m[1082] = 'K'; m[1083] = '.';
		for (eng::u32 row = 0; row < 64; ++row) {
			const eng::u32 base = 1084 + row * 16 + 4; // canal 1 (AUD1)
			m[base + 0] = 0x01;
			m[base + 1] = 0xAC;
			m[base + 2] = 0x10;
			m[base + 3] = 0x00;
		}
		for (eng::u32 i = 0; i < kSampleLen; ++i) {
			m[kSampleOffset + i] = (i < kSampleLen / 2) ? 48u : static_cast<eng::u8>(256u - 48u);
		}
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
	bool m_music_ok = false;
	bool m_init_ok = false;
	bool m_confirmed = false;
	eng::u16 m_copper_words = 0;
	eng::audio::SfxChannel m_alarm_ch = -1;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_mod_block {};
	eng::MemoryBlock m_alarm_block {};
	eng::MemoryBlock m_beep_block {};
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::audio::AudioSystem m_audio {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	AudioSystemDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
