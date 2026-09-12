// ============================================================================
// Demo 063: "harmony" — armonía reconocible: 3 canales de música + SFX mixer.
// ============================================================================
//
// Toca el "Himno a la Alegría" (Beethoven) a 3 voces —melodía, armonía en
// terceras y bajo— por los canales AUD1..AUD3 de ptplayer, mientras el SFX mixer
// (AUD0) reproduce un bajo continuo en bucle. Demuestra los 4 canales de Paula
// trabajando a la vez con notas reconocibles.
//
// La muestra es una onda cuadrada de 32 muestras (bucle), de modo que los
// períodos Protracker (C-2=428..B-2=226) suenan en la octava ~C4..B4
// (261..495 Hz). El bajo del mixer usa una onda cuadrada preprocesada (±24) más
// larga para sonar grave.
//
// Evidencia: `mark_ready` guarda DMACONR cuando los 4 canales de audio están
// activos (AUD0 mixer + AUD1..AUD3 música).

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

// Períodos Protracker (octava C-2..B-2): con una muestra de 64 muestras suenan
// en ~C3..B3 (131..247 Hz).
constexpr eng::u16 kC = 428, kD = 381, kE = 339, kF = 320, kG = 285, kA = 254, kB = 226;
// Octava superior (C-3..B-3) para la melodía (~C4..B4, 262..495 Hz, más clara).
constexpr eng::u16 kC3 = 214, kD3 = 190, kE3 = 170, kF3 = 160, kG3 = 143, kA3 = 127, kB3 = 113;
// Octava inferior (C-1..B-1) para el bajo (~C2..B2, 65..123 Hz).
constexpr eng::u16 kC1 = 856, kD1 = 762, kE1 = 678, kF1 = 640, kG1 = 570, kA1 = 508, kB1 = 453;

constexpr eng::u8  kNotes = 15;
constexpr eng::u32 kSampleBytes = 64;   // un ciclo de seno (64 muestras)
constexpr eng::u16 kSampleWords = kSampleBytes / 2u;

constexpr eng::u32 kModSize = 2108 + kSampleBytes; // patrón 0 + muestra

// "Himno a la Alegría" (melodía en C-3..B-3, ~262..495 Hz).
constexpr eng::u16 kMelody[kNotes]   = { kE3, kE3, kF3, kG3, kG3, kF3, kE3, kD3, kC3, kC3, kD3, kE3, kE3, kD3, kD3 };
// Armonía una octava por debajo (C-2..B-2, ~131..247 Hz).
constexpr eng::u16 kHarmony[kNotes]  = { kE, kE, kF, kG, kG, kF, kE, kD, kC, kC, kD, kE, kE, kD, kD };
// Bajo: tónica por compás (C-1..B-1, ~65..123 Hz).
constexpr eng::u16 kBass[kNotes]     = { kC1, kC1, kC1, kC1, kC1, kC1, kG1, kG1, kF1, kF1, kC1, kC1, kC1, kC1, kC1 };

// Diagnóstico: bytes crudos de la primera nota de la melodía (offset 1088).
volatile eng::u32 g_first_note = 0;

struct HarmonyDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u });
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00006301u); return; }

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		m_mod_block = backend.memory().chip.allocate(kModSize, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_mod_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006302u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);
		build_mod(static_cast<eng::u8*>(m_mod_block.data));

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00006303u); return; }

		backend.takeover_display(m_copper_ptr);

		// Diagnóstico: volcar los 4 bytes de la primera nota de la melodía
		// (offset 1084 + 0*16 + 1*4 = 1088) para verificar la codificación.
		{
			const eng::u8* mod = static_cast<const eng::u8*>(m_mod_block.data);
			g_first_note = (static_cast<eng::u32>(mod[1088]) << 24) |
				(static_cast<eng::u32>(mod[1089]) << 16) |
				(static_cast<eng::u32>(mod[1090]) << 8) |
				static_cast<eng::u32>(mod[1091]);
		}

		// El audio lo posee el backend; el GameAudio se enlaza a él.
		m_audio.attach(backend.audio());

		// Música primero (ptplayer toca todos los canales al arrancar), luego se
		// reserva AUD0 para el mixer y se arranca el mixer.
		eng::audio::MusicModule mod { eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_mod_block.data), kModSize) };
		m_music_ok = m_audio.play_music(mod, eng::audio::MusicFormat::Protracker);
		m_audio.set_music_volume(40);
		m_audio.set_music_channel_mask(0x0Eu); // silencia AUD0 (mixer), deja AUD1..AUD3

		if (!m_audio.init(backend.memory())) { eng::debug::mark_failed(g_eng_run_status, 0x00006304u); return; }

		// NOTA: sin SFX continuo del mixer aquí; el bajo lo da el canal 3 de la
		// música (AUD3). El mixer (AUD0) queda libre para SFX discretos.

		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok || m_confirmed) {
			return;
		}
		m_audio.update(context.frame.frame_index);
		m_audio.update_music();

		// Registra el período del canal 1 en dos instantes para verificar que la
		// melodía cambia de nota (si es un tono fijo, ambos períodos coinciden).
		if (context.frame.frame_index == 20u) {
			m_period_early = m_audio.system().protracker().period();
		}

		if (context.frame.frame_index == 100u) {
			const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
			const eng::u16 period = m_audio.system().protracker().period();   // período canal 1
			if ((dmaconr & 0x0Fu) == 0x0Fu) { // AUD0..AUD3 activos
				// bits 31-16 = período en frame 20, bits 15-0 = período en frame 100.
				eng::debug::mark_ready(g_eng_run_status, (static_cast<eng::u32>(m_period_early) << 16u) | period);
			} else {
				eng::debug::mark_failed(g_eng_run_status, 0x00006305u);
			}
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

	void write_note(eng::u8* m, eng::u32 row, eng::u8 channel, eng::u16 period) {
		// Formato de nota del ptplayer (NO el estándar M.K.):
		//   byte0 = período alto (bits 11-8) en el nibble bajo.
		//   byte1 = período bajo (bits 7-0).
		//   byte2 = (muestra << 4) | efecto.
		//   byte3 = parámetro de efecto.
		const eng::u32 base = 1084 + row * 16u + channel * 4u;
		m[base + 0] = static_cast<eng::u8>(period >> 8);
		m[base + 1] = static_cast<eng::u8>(period & 0xFFu);
		m[base + 2] = 0x10u; // muestra 1, sin efecto
		m[base + 3] = 0x00u;
	}

	/// Módulo Protracker: 1 muestra (cuadrada, bucle) + 1 patrón con 3 voces.
	void build_mod(eng::u8* m) {
		for (eng::u32 i = 0; i < kModSize; ++i) m[i] = 0;

		// Cabecera de la muestra 1: 32 words, volumen 64, bucle completo.
		const eng::u32 h = 20;
		m[h + 22] = 0x00; m[h + 23] = static_cast<eng::u8>(kSampleWords & 0xff); // length (words)
		m[h + 25] = 64;                                                           // volume
		m[h + 28] = 0x00; m[h + 29] = static_cast<eng::u8>(kSampleWords & 0xff);  // loop length (words)

		m[950] = 1;    // song length
		m[951] = 127;  // restart
		m[952] = 0;    // order

		m[1080] = 'M'; m[1081] = '.'; m[1082] = 'K'; m[1083] = '.';

		// Tres voces: melodía (canal 1), armonía (canal 2), bajo (canal 3).
		for (eng::u8 i = 0; i < kNotes; ++i) {
			const eng::u32 row = note_row(i);
			write_note(m, row, 1, kMelody[i]);  // AUD1
			write_note(m, row, 2, kHarmony[i]); // AUD2
			write_note(m, row, 3, kBass[i]);    // AUD3
		}

		// Datos de la muestra (offset 2108): seno ±63 (más limpio que la cuadrada).
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
	HarmonyDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
