// ============================================================================
// Demo 066: "polyphony" — tres voces independientes (lead, contramelodía, bajo).
// ============================================================================
//
// Cada canal de ptplayer lleva su PROPIA secuencia rítmica y melódica, con su
// propio timbre (sample) y su propio efecto: melodía aguda en corcheas con
// vibrato (AUD1), contramelodía sincopada con vibrato más lento (AUD2) y bajo en
// corcheas con trémolo (AUD3). Es polifonía real (tres líneas simultáneas), no
// acordes de bloque.
//
// La armonía sigue Am - F - C - G (16 filas por compás, 64 filas = 4 compases).
//
// Evidencia: `mark_ready` solo se marca si DMACONR confirma AUD1..AUD3 activos.
// `detail` guarda el período del lead en los frames 20 y 100 (debe cambiar) y
// `frame` los períodos de contramelodía y bajo en el frame 100.

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

constexpr eng::u32 kSampleBytes = 64;                            // un ciclo por sample
constexpr eng::u16 kSampleWords = kSampleBytes / 2u;
constexpr eng::u32 kPatternOffset = 1084;
constexpr eng::u32 kPatternRows = 64u;
constexpr eng::u32 kPatternBytes = kPatternRows * 16u;           // 64 filas x 4 canales
constexpr eng::u32 kSampleBase = kPatternOffset + kPatternBytes; // 2108
constexpr eng::u32 kNumSamples = 3;
constexpr eng::u32 kModSize = kSampleBase + kNumSamples * kSampleBytes;

// Efectos ProTracker (nibble + parámetro xy).
constexpr eng::u8 kFxNone    = 0x0;
constexpr eng::u8 kFxVibrato = 0x4;
constexpr eng::u8 kFxTremolo = 0x7;

// Períodos ProTracker por octava. LÍMITE IMPORTANTE: la tabla de períodos de
// ptplayer (mt_PeriodTable) cubre solo C-1..B-3 = períodos 856..113; una nota
// más aguda (período < 113) queda fuera y el lookup falla (clava B-3). Por eso
// las tres voces se mantienen dentro de ese rango.
constexpr eng::u16 C1 = 856, D1 = 762, E1 = 678, F1 = 640, G1 = 570, A1 = 508, B1 = 453;
constexpr eng::u16 C2 = 428, D2 = 381, E2 = 339, F2 = 320, G2 = 285, A2 = 254, B2 = 226;
constexpr eng::u16 C3 = 214, D3 = 190, E3 = 170, F3 = 160, G3 = 143, A3 = 127, B3 = 113;

struct Note { eng::u8 row; eng::u16 period; };

// Lead (canal 1, AUD1): corcheas en filas pares, registro agudo. Vibrato 0x48.
constexpr Note kLead[] = {
	{ 0, E3}, { 2, A3}, { 4, G3}, { 6, E3}, { 8, C3}, {10, D3}, {12, E3}, {14, D3},
	{16, C3}, {18, F3}, {20, E3}, {22, C3}, {24, A2}, {26, C3}, {28, D3}, {30, C3},
	{32, E3}, {34, G3}, {36, E3}, {38, C3}, {40, D3}, {42, E3}, {44, G3}, {46, E3},
	{48, D3}, {50, G3}, {52, F3}, {54, D3}, {56, B2}, {58, D3}, {60, G3}, {62, D3},
};

// Contramelodía (canal 2, AUD2): síncopas en filas 1,5,9,13, registro medio.
// Vibrato más lento (0x64) para diferenciarla del lead.
constexpr Note kCounter[] = {
	{ 1, A2}, { 5, E2}, { 9, G2}, {13, A2},
	{17, F2}, {21, A2}, {25, C3}, {29, A2},
	{33, G2}, {37, E2}, {41, C3}, {45, E2},
	{49, D2}, {53, G2}, {57, B2}, {61, D2},
};

// Bajo (canal 3, AUD3): corcheas (tónica-quinta), registro grave. Trémolo 0x74
// para que pulse y se distinga aunque esté por debajo de las otras voces.
constexpr Note kBass[] = {
	{ 0, A1}, { 2, E1}, { 4, A1}, { 6, E1}, { 8, A1}, {10, E1}, {12, A1}, {14, E1},
	{16, F1}, {18, C2}, {20, F1}, {22, C2}, {24, F1}, {26, C2}, {28, F1}, {30, C2},
	{32, C2}, {34, G1}, {36, C2}, {38, G1}, {40, C2}, {42, G1}, {44, C2}, {46, G1},
	{48, G1}, {50, D2}, {52, G1}, {54, D2}, {56, G1}, {58, D2}, {60, G1}, {62, D2},
};

struct PolyphonyDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u });
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00006601u); return; }

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		m_mod_block = backend.memory().chip.allocate(kModSize, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_mod_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006602u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);
		build_mod(static_cast<eng::u8*>(m_mod_block.data));

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00006603u); return; }
		backend.takeover_display(m_copper_ptr);

		m_audio.attach(backend.audio());
		eng::audio::MusicModule mod { eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_mod_block.data), kModSize) };
		m_music_ok = m_audio.play_music(mod, eng::audio::MusicFormat::Protracker);
		// Máscara: bit a 1 = canal audible. 0x0E deja sonar AUD1..AUD3.
		m_audio.set_music_channel_mask(0x0Eu);
		m_audio.set_music_volume(48);

		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok || m_confirmed) {
			return;
		}
		m_audio.update(context.frame.frame_index);
		m_audio.update_music();

		eng::audio::PtPlayer& pt = m_audio.system().protracker();
		if (context.frame.frame_index == 20u) {
			m_lead_early = pt.channel_period(1);
		}

		if (context.frame.frame_index == 100u) {
			const eng::u16 dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
			if ((dmaconr & 0x0Eu) != 0x0Eu) { // AUD1..AUD3 activos
				eng::debug::mark_failed(g_eng_run_status, 0x00006605u);
				m_confirmed = true;
				return;
			}
			const eng::u16 lead = pt.channel_period(1);
			const eng::u16 counter = pt.channel_period(2);
			const eng::u16 bass = pt.channel_period(3);
			eng::debug::mark_ready(g_eng_run_status, (static_cast<eng::u32>(m_lead_early) << 16u) | lead);
			g_eng_run_status.frame = (static_cast<eng::u32>(counter) << 16u) | bass;
			m_confirmed = true;
		}
		(void)backend;
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Cabecera de sample (30 bytes): longitud, finetune, volumen y bucle completo.
	void write_sample_header(eng::u8* m, eng::u32 idx, eng::u16 words, eng::u8 volume) {
		const eng::u32 h = 20u + idx * 30u;
		m[h + 22] = static_cast<eng::u8>((words >> 8) & 0xffu);
		m[h + 23] = static_cast<eng::u8>(words & 0xffu);
		m[h + 24] = 0;                                          // finetune
		m[h + 25] = volume;                                     // volumen (0..64)
		m[h + 26] = 0; m[h + 27] = 0;                           // loop start = 0
		m[h + 28] = static_cast<eng::u8>((words >> 8) & 0xffu);
		m[h + 29] = static_cast<eng::u8>(words & 0xffu);
	}

	/// Nota + efecto. Formato de nota del ptplayer (NO M.K.): byte0 = período
	/// alto, byte1 = período bajo, byte2 = (muestra << 4) | efecto, byte3 = efecto.
	void write_note(eng::u8* m, eng::u32 row, eng::u8 channel, eng::u16 period,
	                eng::u8 sample, eng::u8 effect, eng::u8 param) {
		const eng::u32 base = kPatternOffset + row * 16u + channel * 4u;
		m[base + 0] = static_cast<eng::u8>(period >> 8);
		m[base + 1] = static_cast<eng::u8>(period & 0xffu);
		m[base + 2] = static_cast<eng::u8>((sample << 4) | (effect & 0x0fu));
		m[base + 3] = param;
	}

	/// Fila de solo-efecto (sin nota nueva): continúa un efecto sobre la nota
	/// que ya está sonando (byte2 sin nibble de muestra).
	void write_fx(eng::u8* m, eng::u32 row, eng::u8 channel, eng::u8 effect, eng::u8 param) {
		const eng::u32 base = kPatternOffset + row * 16u + channel * 4u;
		m[base + 0] = 0;
		m[base + 1] = 0;
		m[base + 2] = static_cast<eng::u8>(effect & 0x0fu);
		m[base + 3] = param;
	}

	/// Escribe una voz: las notas y, en las filas intermedias hasta la siguiente
	/// nota, el mismo efecto (para que vibrato/trémolo sigan sonando).
	void write_voice(eng::u8* m, const Note* notes, eng::u32 count, eng::u8 channel,
	                 eng::u8 sample, eng::u8 effect, eng::u8 param) {
		for (eng::u32 i = 0; i < count; ++i) {
			write_note(m, notes[i].row, channel, notes[i].period, sample, effect, param);
		}
		for (eng::u32 i = 0; i < count; ++i) {
			const eng::u32 start = notes[i].row + 1u;
			const eng::u32 end = (i + 1u < count) ? notes[i + 1u].row : kPatternRows;
			for (eng::u32 r = start; r < end; ++r) {
				write_fx(m, r, channel, effect, param);
			}
		}
	}

	/// Timbre = fundamental + armónicos (aritmética entera, sin float),
	/// normalizado por `norm`. Cada voz usa un timbre distinto para separarse.
	void fill_sample(eng::u8* m, eng::u32 idx, eng::s16 h1, eng::s16 h2, eng::s16 h3, eng::s16 norm) {
		eng::u8* dst = m + kSampleBase + idx * kSampleBytes;
		for (eng::u32 i = 0; i < kSampleBytes; ++i) {
			const eng::s32 v = static_cast<eng::s32>(h1) * eng::audio::sine_byte(i)
				+ static_cast<eng::s32>(h2) * eng::audio::sine_byte(2u * i)
				+ static_cast<eng::s32>(h3) * eng::audio::sine_byte(3u * i);
			dst[i] = static_cast<eng::u8>(static_cast<eng::s8>(v / norm));
		}
	}

	void build_mod(eng::u8* m) {
		for (eng::u32 i = 0; i < kModSize; ++i) m[i] = 0;

		// Muestra 1 = lead (reed), 2 = contramelodía (flute), 3 = bajo (organ).
		// Volúmenes balanceados: el bajo más alto porque percibe menos.
		write_sample_header(m, 0, kSampleWords, 30);
		write_sample_header(m, 1, kSampleWords, 40);
		write_sample_header(m, 2, kSampleWords, 60);

		m[950] = 1;    // song length
		m[951] = 0;    // restart position
		m[952] = 0;    // order
		m[1080] = 'M'; m[1081] = '.'; m[1082] = 'K'; m[1083] = '.';

		// Voz 1: vibrato 0x48. Voz 2: vibrato 0x64. Voz 3: trémolo 0x74.
		write_voice(m, kLead, sizeof(kLead) / sizeof(kLead[0]), 1, 1, kFxVibrato, 0x48);
		write_voice(m, kCounter, sizeof(kCounter) / sizeof(kCounter[0]), 2, 2, kFxVibrato, 0x64);
		write_voice(m, kBass, sizeof(kBass) / sizeof(kBass[0]), 3, 3, kFxTremolo, 0x74);

		fill_sample(m, 0, 100, 40, 20, 160); // lead: brillante
		fill_sample(m, 1, 100, 15,  0, 115); // contramelodía: suave
		fill_sample(m, 2, 100, 30, 15, 145); // bajo: fundamental fuerte
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
	eng::u16 m_lead_early = 0;
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
	PolyphonyDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
