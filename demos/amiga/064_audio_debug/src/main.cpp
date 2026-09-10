// ============================================================================
// Demo 064: "audio debug" — onda senoidal pura en UN canal (prueba mínima).
// ============================================================================
//
// Primer eslabón del procedimiento de depuración de sonido: reproducir una onda
// senoidal pura por UN canal de Paula (AUD0), SIN mixer ni música, y exponer por
// el canal lateral (vía `g_audio_dbg`) el estado real del hardware para
// comprobar que la salida es lo que se pretendía.
//
// Qué hace: genera un ciclo de seno (64 muestras, ±127) y lo reproduce en bucle
// en AUD0 con período 127 (~440 Hz). `mark_ready` guarda DMACONR.
//
// Evidencia (leer `g_audio_dbg` por el canal lateral, o los primeros bytes en
// `detail`): puntero de muestra, AUD0LEN/PER/VOL (espejo RAM, son write-only),
// y checksum/mín/máx/media del seno para comparar con el generador host.

#include <eng/audio/wave_tables.hpp>
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

// Diagnóstico de audio leído por el canal lateral (`mem <addr> <len>`).
__attribute__((used)) volatile struct AudioDbg {
	eng::u32 sample_addr;   // dirección de la muestra en Chip RAM
	eng::u16 aud0_len;      // AUD0LEN (words)
	eng::u16 aud0_per;      // AUD0PER (período)
	eng::u16 aud0_vol;      // AUD0VOL
	eng::u16 dmaconr;       // DMACONR (DMA de audio activo)
	eng::u32 checksum;      // suma de los bytes de la muestra
	eng::s16 sample_min;    // mín. de la muestra
	eng::s16 sample_max;    // máx. de la muestra
	eng::s16 sample_mean;   // media de la muestra
} g_audio_dbg {};
}

namespace {

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u32 kSampleBytes = 64;   // un ciclo de seno (64 muestras)
constexpr eng::u16 kSampleWords = kSampleBytes / 2u;
constexpr eng::u16 kPeriod = 214;       // C-4 (~259 Hz) con un ciclo de 64 muestras

// Escala de Do mayor (C D E F G A), una nota cada kNoteFrames frames. Períodos
// C-3..A-3 con un ciclo de 64 muestras suenan ~C4..A4 (259..436 Hz).
constexpr eng::u16 kScale[6] = { 214, 190, 170, 160, 143, 127 };
constexpr eng::u32 kNoteFrames = 20;    // frames por nota

struct AudioDebugDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u });
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00006401u); return; }

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		m_sample_block = backend.memory().chip.allocate(kSampleBytes, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sample_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006402u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00006403u); return; }
		backend.takeover_display(m_copper_ptr);

		// Genera un ciclo de seno y calcula el diagnóstico.
		eng::u8* sample = static_cast<eng::u8*>(m_sample_block.data);
		eng::u32 sum = 0;
		eng::s16 mn = 0, mx = 0;
		eng::s32 mean_sum = 0;
		for (eng::u32 i = 0; i < kSampleBytes; ++i) {
			const eng::s8 v = eng::audio::sine_byte(i);
			sample[i] = static_cast<eng::u8>(v);
			sum += static_cast<eng::u8>(v);
			mean_sum += v;
			if (static_cast<eng::s16>(v) < mn) mn = v;
			if (static_cast<eng::s16>(v) > mx) mx = v;
		}

		// Programa AUD0 para reproducir la muestra en bucle.
		const eng::u32 addr = reinterpret_cast<eng::u32>(sample);
		*reinterpret_cast<volatile eng::u16*>(0xdff0a0u) = static_cast<eng::u16>(addr >> 16);  // AUD0LCH
		*reinterpret_cast<volatile eng::u16*>(0xdff0a2u) = static_cast<eng::u16>(addr & 0xffffu); // AUD0LCL
		*reinterpret_cast<volatile eng::u16*>(0xdff0a4u) = kSampleWords;  // AUD0LEN
		*reinterpret_cast<volatile eng::u16*>(0xdff0a8u) = 64;            // AUD0VOL
		*reinterpret_cast<volatile eng::u16*>(0xdff0a6u) = kPeriod;       // AUD0PER
		*reinterpret_cast<volatile eng::u16*>(0xdff096u) = 0x8201u;       // DMACON: SET|MASTER|AUD0

		// Rellena el diagnóstico (leíble por canal lateral).
		g_audio_dbg.sample_addr = addr;
		g_audio_dbg.aud0_len = kSampleWords;
		g_audio_dbg.aud0_per = kPeriod;
		g_audio_dbg.aud0_vol = 64;
		g_audio_dbg.dmaconr = *reinterpret_cast<volatile eng::u16*>(0xdff002u);
		g_audio_dbg.checksum = sum;
		g_audio_dbg.sample_min = mn;
		g_audio_dbg.sample_max = mx;
		g_audio_dbg.sample_mean = static_cast<eng::s16>(mean_sum / static_cast<eng::s32>(kSampleBytes));

		// Evidencia compacta en `detail` (leíble del run-report):
		//   bits 31-16 = DMACONR, bits 15-8 = sample_max, bits 7-0 = sample_min.
		eng::debug::mark_ready(
			g_eng_run_status,
			(static_cast<eng::u32>(g_audio_dbg.dmaconr) << 16u) |
			(static_cast<eng::u32>(g_audio_dbg.sample_max & 0xFF) << 8u) |
			(static_cast<eng::u32>(g_audio_dbg.sample_min) & 0xFFu)
		);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		// Cambia el tono cada kNoteFrames frames: una escala de Do mayor. Esto
		// demuestra que el tono de UN canal de Paula cambia (sin ptplayer ni mixer).
		const eng::u8 note = static_cast<eng::u8>((context.frame.frame_index / kNoteFrames) % 6u);
		const eng::u16 period = kScale[note];
		*reinterpret_cast<volatile eng::u16*>(0xdff0a6u) = period; // AUD0PER
		g_audio_dbg.aud0_per = period;
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
	eng::u16 m_copper_words = 0;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_sample_block {};
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	AudioDebugDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
