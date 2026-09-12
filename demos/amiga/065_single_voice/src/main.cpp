// ============================================================================
// Demo 065: "single voice" — melodía de UNA voz por ptplayer (sin armonía).
// ============================================================================
//
// Paso siguiente a la demo 064 (escala por Paula directa): reproducir el "Himno
// a la Alegría" por UN SOLO canal (AUD1) usando el reproductor ptplayer, con la
// codificación de nota correcta (byte0=período alto, byte1=período bajo,
// byte2=muestra). Sin mixer, sin armonía, sin bajo.
//
// Evidencia: `mark_ready` guarda el período del canal 1 en dos instantes
// (frame 20 y frame 100) para confirmar que la melodía cambia de nota.

#include <eng/audio/game_audio.hpp>
#include <eng/audio/wave_tables.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga_minimal.hpp>

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

constexpr eng::u32 kSampleBytes = 64;   // un ciclo de seno (64 muestras)
constexpr eng::u16 kSampleWords = kSampleBytes / 2u;
constexpr eng::u32 kModSize = 2108 + kSampleBytes; // patrón 0 + muestra

// Períodos Protracker (octava C-3..B-3), con un ciclo de 64 muestras suenan
// ~C4..B4 (259..495 Hz).
constexpr eng::u16 kC = 214, kD = 190, kE = 170, kF = 160, kG = 143, kA = 127, kB = 113;

constexpr eng::u8 kNotes = 15;

// "Himno a la Alegría": E E F G | G F E D | C C D E | E D D.
constexpr eng::u16 kMelody[kNotes] = { kE, kE, kF, kG, kG, kF, kE, kD, kC, kC, kD, kE, kE, kD, kD };

struct SingleVoiceDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u });
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00006501u); return; }

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		m_mod_block = backend.memory().chip.allocate(kModSize, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_mod_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006502u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);
		build_mod(static_cast<eng::u8*>(m_mod_block.data));

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00006503u); return; }
		backend.takeover_display(m_copper_ptr);

		// Música de una sola voz por ptplayer en el canal 1 (AUD1).
		m_audio.attach(backend.audio());
		eng::audio::MusicModule mod { eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_mod_block.data), kModSize) };
		m_music_ok = m_audio.play_music(mod, eng::audio::MusicFormat::Protracker);
		// Máscara de canales del ptplayer: bit a 1 = canal audible. 0x02 silencia
		// AUD0/AUD2/AUD3 y deja sonar solo AUD1 (la melodía).
		m_audio.set_music_channel_mask(0x02u);
		m_audio.set_music_volume(48);

		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok || m_confirmed) {
			return;
		}
		m_audio.update(context.frame.frame_index);
		m_audio.update_music();

		if (context.frame.frame_index == 20u) {
			m_period_early = m_audio.system().protracker().period();
		}

		if (context.frame.frame_index == 100u) {
			const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
			const eng::u16 period = m_audio.system().protracker().period();
			// Evidencia del canal lateral: bits 31-16 = período en frame 20,
			// bits 15-0 = período en frame 100 (la melodía cambia E->G).
			eng::debug::mark_ready(g_eng_run_status, (static_cast<eng::u32>(m_period_early) << 16u) | period);

			// DMACONR sí es legible: sus bits 0-3 reflejan el DMA de AUD0..AUD3.
			// Con la máscara 0x02 solo AUD1 queda activo (bit 1 = 0x0002).
			// (AUDxPER/AUDxVOL son write-only; el período se lee del reproductor
			// vía `period()`, no de los registros hardware.)
			g_eng_run_status.frame = static_cast<eng::u32>(dmaconr);
			m_confirmed = true;
		}
		(void)backend;
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Fila de inicio de la nota `i` (13 notas de 4 filas + 2 de 6 filas = 64).
	eng::u32 note_row(eng::u8 i) {
		return (i < 13) ? static_cast<eng::u32>(i) * 4u : 52u + static_cast<eng::u32>(i - 13) * 6u;
	}

	/// Codificación de nota del ptplayer: byte0=período alto, byte1=período bajo,
	/// byte2=muestra, byte3=efecto.
	void write_note(eng::u8* m, eng::u32 row, eng::u8 channel, eng::u16 period) {
		const eng::u32 base = 1084 + row * 16u + channel * 4u;
		m[base + 0] = static_cast<eng::u8>(period >> 8);
		m[base + 1] = static_cast<eng::u8>(period & 0xFFu);
		m[base + 2] = 0x10u; // muestra 1
		m[base + 3] = 0x00u;
	}

	void build_mod(eng::u8* m) {
		for (eng::u32 i = 0; i < kModSize; ++i) m[i] = 0;

		// Cabecera de la muestra 1: 32 words, volumen 64, bucle completo.
		const eng::u32 h = 20;
		m[h + 22] = 0x00; m[h + 23] = static_cast<eng::u8>(kSampleWords & 0xff); // length
		m[h + 25] = 64;                                                           // volume
		m[h + 28] = 0x00; m[h + 29] = static_cast<eng::u8>(kSampleWords & 0xff);  // loop length

		m[950] = 1;    // song length
		m[951] = 127;  // restart
		m[952] = 0;    // order
		m[1080] = 'M'; m[1081] = '.'; m[1082] = 'K'; m[1083] = '.';

		// Melodía en el canal 1 (AUD1).
		for (eng::u8 i = 0; i < kNotes; ++i) {
			write_note(m, note_row(i), 1, kMelody[i]);
		}

		// Muestra: seno ±63.
		for (eng::u32 i = 0; i < kSampleBytes; ++i) {
			m[2108 + i] = static_cast<eng::u8>(eng::audio::sine_byte(i) / 2);
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
	eng::u16 m_period_early = 0;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_mod_block {};
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::audio::GameAudio m_audio {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	SingleVoiceDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
