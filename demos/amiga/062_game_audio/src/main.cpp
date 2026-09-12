// ============================================================================
// Demo 062: "game audio" — capa de audio de juego (GameAudio: banco + política).
// ============================================================================
//
// Demuestra la capa orientada a juego `eng::audio::GameAudio` por encima del
// `AudioSystem`: el juego registra sonidos en un `SampleBank` por `id` y los
// dispara con `play(id)` (cooldown, límite de instancias, prioridad) mientras
// suena música Protracker, con ducking (la música baja cuando suena la alarma).
//
// Qué suena: música en bucle (canal AUD1) + alarma en bucle con ducking (mixer,
// AUD0). FIRE dispara un "beep" con cooldown. Evidencia: `mark_ready` guarda
// DMACONR cuando el DMA de audio y música está activo.

#include <eng/audio/game_audio.hpp>
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

constexpr eng::u8 kSfxBeep = 0;
constexpr eng::u8 kSfxAlarm = 1;

struct GameAudioDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u });
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00006201u); return; }

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		m_mod_block = backend.memory().chip.allocate(kModSize, 4);
		m_alarm_block = backend.memory().chip.allocate(kAlarmLen, 4);
		m_beep_block = backend.memory().chip.allocate(kBeepLen, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_mod_block.valid() ||
			!m_alarm_block.valid() || !m_beep_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006202u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);
		build_mod(static_cast<eng::u8*>(m_mod_block.data));
		gen_square(static_cast<eng::u8*>(m_alarm_block.data), kAlarmLen, 64);
		gen_square(static_cast<eng::u8*>(m_beep_block.data), kBeepLen, 4);

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00006203u); return; }

		backend.takeover_display(m_copper_ptr);

		// El audio lo posee el backend; el GameAudio (capa de juego) se enlaza a él.
		m_audio.attach(backend.audio());

		// Música primero, luego reservar AUD0 para el mixer, luego el mixer.
		eng::audio::MusicModule mod { eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_mod_block.data), kModSize) };
		m_music_ok = m_audio.play_music(mod, eng::audio::MusicFormat::Protracker);
		m_audio.set_music_volume(40);
		m_audio.set_duck_volume(12);

		if (!m_audio.init(backend.memory())) { eng::debug::mark_failed(g_eng_run_status, 0x00006204u); return; }
		// El mixer usa AUD0; la música AUD1 (módulo con la nota en el canal 1).
		m_audio.set_music_channel_mask(0x01u);

		// Banco: beep (cooldown 4 frames, max 2) y alarma (bucle, ducking).
		m_audio.bank().add(kSfxBeep, {
			eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_beep_block.data), kBeepLen),
			3, 2, 4, false
		});
		m_audio.bank().add(kSfxAlarm, {
			eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_alarm_block.data), kAlarmLen),
			1, 1, 0, true // duck_music
		});
		m_alarm_ch = m_audio.play(kSfxAlarm, 0);

		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok || m_confirmed) {
			return;
		}

		m_audio.update(context.frame.frame_index); // poda voces + ducking
		m_audio.update_music();

		eng::amiga::GameInput gin;
		eng::amiga::poll_input(gin);
		if (gin.port0 != 0u) {
			m_audio.play(kSfxBeep, context.frame.frame_index);
		}

		if (context.frame.frame_index == 100u) {
			const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
			if ((dmaconr & 0x0Fu) != 0u) {
				eng::debug::mark_ready(g_eng_run_status, (static_cast<eng::u32>(dmaconr) << 16u) | 1u);
			} else {
				eng::debug::mark_failed(g_eng_run_status, 0x00006205u);
			}
			m_confirmed = true;
		}
		(void)backend;
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
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
	eng::audio::GameAudio m_audio {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	GameAudioDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
