// ============================================================================
// Demo 067: "mixer melody" — UNA melodía por UNA voz del mixer (AUD0).
// ============================================================================
//
// Primer paso del plan incremental del mixer de SFX (Audio Mixer 3.7): poner UNA
// sola voz software del mixer (MixCh0) reproduciendo una melodía en bucle, con
// las otras tres voces del mixer y los canales hardware AUD1..AUD3 en silencio.
//
// El mixer no tiene control de tono por voz (el plugin de pitch está desactivado
// en `mixer_config.i`), así que cada "instrumento" se PRE-RENDERIZA en una
// muestra: aquí se sintetiza el "Himno a la Alegría" (E E F G G F E D) muestra a
// muestra a ~11 kHz con un acumulador de fase entero, amplitud ±120 (con UNA
// sola voz no hace falta reservar headroom para 4; el límite ±32 solo aplica
// cuando suman varias voces) y longitud múltiplo de 4.
//
// Siguiente paso (futuro): añadir una segunda voz pre-renderizada, y así.
//
// Evidencia: `mark_ready` guarda DMACONR (bit 0 = AUD0EN) y el canal devuelto.

#include <eng/audio/sfx_mixer.hpp>
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

// --- Melodía pre-renderizada -------------------------------------------------
constexpr eng::u32 kSampleRate = 11025;  // ritmo de salida del mixer (period 322)
constexpr eng::u32 kNoteSamples = 1600;  // ~0.145 s por nota
constexpr eng::u16 kFreq[] = { 330, 330, 349, 392, 392, 349, 330, 294 }; // E E F G G F E D
constexpr eng::u32 kNoteCount = sizeof(kFreq) / sizeof(kFreq[0]);
constexpr eng::u32 kSampleLen = kNoteCount * kNoteSamples; // 12800 (múltiplo de 4)
constexpr eng::s32 kVoices = 1;          // voces simultáneas previstas
constexpr eng::s32 kAmplitude = 120 / kVoices; // 1 voz -> ±120; 4 voces -> ±30

struct MixerMelodyDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({ 96u * 1024u, 8u * 1024u, 4u * 1024u });
		if (!m_memory_ok) { eng::debug::mark_failed(g_eng_run_status, 0x00006701u); return; }

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		m_melody_block = backend.memory().chip.allocate(kSampleLen, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_melody_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006702u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);
		eng::audio::synth_sequence<kNoteSamples>(static_cast<eng::u8*>(m_melody_block.data), kFreq, kNoteCount, kSampleRate, static_cast<eng::s16>(kAmplitude));

		if (!build_copper()) { eng::debug::mark_failed(g_eng_run_status, 0x00006703u); return; }

		// Orden: primero el display (congela el sistema), luego el mixer.
		backend.takeover_display(m_copper_ptr);

		if (!m_sfx.init(backend.memory())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006704u);
			return;
		}
		m_sfx.set_master_volume(64);

		// UNA voz: la melodía en MixCh0. Las otras tres quedan libres (silencio).
		m_channel = m_sfx.play(melody_sample(), 1, eng::audio::LoopMode::Loop);
		if (m_channel < 0) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006705u);
			return;
		}
		m_sfx.reset_counter();

		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (!m_init_ok || m_confirmed) {
			return;
		}
		// Diagnóstico: a los 60 frames cuenta cuántas veces ha corrido la
		// interrupción del mixer (si es 0, el mixer no se está ejecutando).
		if (context.frame.frame_index >= 60u) {
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
			// detail: bits 31-24 = pico, 23-16 = valle, 15-0 = cruces (buffer).
			eng::debug::mark_ready(g_eng_run_status,
				(static_cast<eng::u32>(vmax) << 24u) |
				(static_cast<eng::u32>(vmin) << 16u) |
				(static_cast<eng::u32>(changes & 0xffffu)));
			g_eng_run_status.frame = static_cast<eng::u32>(m_sfx.counter());
			m_confirmed = true;
		}
		(void)backend;
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		draw_scope();
		// Reinstalar la copperlist cada frame (como 058): el mixer/arranque puede
		// dejar el DMA de bitplanes apagado y la lista, al reiniciarse, lo reactiva.
		if (build_copper()) backend.install_copper_list(m_copper_ptr);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Osciloscopio: dibuja el buffer de mezcla en el plano 0 (blanco) para ver
	/// en la captura si Paula recibe una onda bipolar (centro y ≈ 127) o DC/ruido.
	/// La línea central (silencio = 128) va en el plano 1 (verde) como referencia.
	void draw_scope() {
		const eng::u8* buf = m_sfx.buffer();
		const eng::u32 n = m_sfx.buffer_bytes();
		for (eng::u32 i = 0; i < kPlaneBytes; ++i) m_bitplanes[i] = 0;
		for (eng::u32 i = 0; i < kPlaneBytes; ++i) m_bitplanes[kPlaneBytes + i] = 0;
		if (n == 0u) return;
		for (eng::u16 x = 0; x < 320u; ++x) {
			const eng::u32 si = (static_cast<eng::u32>(x) * n) / 320u;
			const eng::u16 y = static_cast<eng::u16>(255u - buf[si]);
			set_pixel(m_bitplanes, x, y);
			if (y + 1u < 256u) set_pixel(m_bitplanes, x, static_cast<eng::u16>(y + 1u));
		}
		// Línea central de referencia (silencio = 128 -> y = 127), en verde.
		for (eng::u16 x = 0; x < 320u; ++x) set_pixel(m_bitplanes + kPlaneBytes, x, 127u);
	}

	void set_pixel(eng::u8* plane, eng::u16 x, eng::u16 y) {
		if (x >= 320u || y >= 256u) return;
		plane[static_cast<eng::u32>(y) * kBytesPerRow + (x >> 3u)]
			|= static_cast<eng::u8>(1u << (7u - (x & 7u)));
	}
	eng::audio::SfxSample melody_sample() {
		return { eng::Span<const eng::u8>(static_cast<const eng::u8*>(m_melody_block.data), kSampleLen) };
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
		sched.move(eng::copper::color_register(0), 0x000fu); // fondo azul (prueba de display)
		sched.move(eng::copper::color_register(1), 0x0fffu); // trazo del osciloscopio
		sched.move(eng::copper::color_register(2), 0x00f0u); // línea central
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
	eng::u16 m_copper_words = 0;
	eng::audio::SfxChannel m_channel = -1;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_melody_block {};
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::audio::SfxMixer m_sfx {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	MixerMelodyDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
