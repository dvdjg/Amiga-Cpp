// ============================================================================
// Demo 272: streaming digital PCM desde RAM (A5) — `PcmStream` + IRQ de audio.
// ============================================================================
//
// Ejercita en hardware la maquinaria de A5 sin depender del disco (que cuelga en
// `td_open`, ver docs/debugging): la "fuente" es una melodia sintetizada en el
// propio 68000, troceada en chunks y comprimida con el codec real (Delta+RLE).
//
// Flujo:
//   - 2 buffers PCM en Chip (`PcmStream<2>`, `AUDxLEN = muestras/2` palabras);
//   - la demo sintetiza + codifica el chunk en el bucle principal (`provide`) y lo
//     deja listo en el buffer libre;
//   - la **IRQ de audio** (nivel 4, voz 3) salta cuando Paula agota el buffer: el
//     servicio llama a `advance()` y reprograma `AUDxLCH/LCL/LEN` con el nuevo
//     buffer (cambio de puntero sin parar el DMA). No descomprime.
//
// **Estado: WIP, sin verificar.** El informe objetivo es `mark_ready` en el frame
// `kFrameReport` si hubo IRQs, cambios de buffer y cero underruns (`detail = (irq << 16) |
// swaps`); hoy NO se cumple: la IRQ de audio dispara ~34x mas rapido que `AUDxPER * AUDxLEN`
// (underruns). Hallazgos y siguientes pasos: docs/debugging/audio-stream-irq-rate.md.
// Ver tambien docs/engine/architecture/AUDIO_STREAMING.md y ROADMAP_AUDIO.md (A5).
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/audio/audio_mode.hpp>
#include <eng/audio/pcm_codec.hpp>
#include <eng/audio/pcm_stream.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/platform/audio_paula.hpp>

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
constexpr eng::u16 kBarRow = 140u;

constexpr eng::u16 kChunkSamples = 2048u; ///< muestras PCM por chunk (buffer de 2 KB)
constexpr eng::u8 kNumBuffers = 2u;
constexpr eng::u8 kChannel = 3u;          ///< voz de Paula del stream
constexpr eng::u16 kRateHz = 16000u;
constexpr eng::u8 kVolume = 48u;
constexpr eng::u16 kMaxComp = 640u;       ///< cota del chunk comprimido (Delta+RLE)

constexpr eng::u8 kMelodyChunks = 8u; ///< chunks de la melodia (se repite en bucle)
constexpr eng::u32 kHalf[8] = {40u, 36u, 32u, 28u, 36u, 40u, 48u, 32u}; ///< semiciclos/muestra
constexpr eng::u16 kStreamChunks = 240u; ///< longitud del "archivo" virtual (~15 s, sin EOF)

constexpr eng::u32 kFrameReport = 240u; ///< ~4 s

/// Buffer de sintesis a nivel de fichero (no en la pila del CLI: la ISR de audio comparte esa
/// pila y 2 kB mas la harian rebosar).
eng::u8 g_scratch[kChunkSamples] {};

struct AudioStreamDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 128u * 1024u, 16u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027201u);
			return;
		}
		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kPlaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(1024, 16);
		m_pcm0 = backend.memory().chip.allocate_block<eng::AudioTag>(kChunkSamples, 4);
		m_pcm1 = backend.memory().chip.allocate_block<eng::AudioTag>(kChunkSamples, 4);
		m_comp = backend.memory().chip.allocate_block<eng::AudioTag>(kMaxComp, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_pcm0.valid() ||
		    !m_pcm1.valid() || !m_comp.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027202u);
			return;
		}
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027203u);
			return;
		}
		backend.takeover_display(m_copper_ptr);

		// Stream: 2 buffers PCM del llamador (Chip); melodia en bucle de kNumChunks chunks.
		eng::audio::PcmStream<kNumBuffers>::Config cfg {
			kRateHz, kChunkSamples, kStreamChunks,
			static_cast<eng::u8>(eng::audio::pcm_codec::Codec::DeltaRle)};
		eng::Span<eng::u8> bufs[kNumBuffers] = {
			eng::Span<eng::u8>(m_pcm0.view.data(), kChunkSamples),
			eng::Span<eng::u8>(m_pcm1.view.data(), kChunkSamples)};
		m_stream.begin(cfg, bufs);

		// 1) Servicio de la IRQ de audio ANTES de arrancar el DMA (arma INTENA de AUD0..3).
		if (!backend.set_audio_service(&AudioStreamDemo::audio_service, *this)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027204u);
			return;
		}
		// 2) Precargar los buffers antes de que suene (evita el underrun del arranque).
		refill();
		// 3) Programar la voz de Paula y encender el DMA.
		m_paula.silence();
		m_paula.set_period(kChannel, eng::audio::paula::period_for_hz(kRateHz));
		m_paula.set_volume(kChannel, kVolume);
		m_paula.set_buffer(kChannel, m_stream.play_pcm(), kChunkSamples / 2u);
		m_paula.start_channel(kChannel);
		m_init_ok = true;
	}

	void update(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		if (!m_init_ok) {
			return;
		}
		refill();
		draw_bar();
		if (context.frame.frame_index == kFrameReport) {
			if (m_irq > 0u && m_swap > 0u && m_underrun == 0u) {
				eng::debug::mark_ready(
					g_eng_run_status,
					(static_cast<eng::u32>(m_irq) << 16u) | (m_swap & 0xffffu));
			} else {
				eng::debug::mark_failed(
					g_eng_run_status,
					((static_cast<eng::u32>(m_irq)) << 16u) |
						((static_cast<eng::u32>(m_swap)) << 8u) |
						static_cast<eng::u32>(m_underrun));
			}
		}
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Trampolín `Service<AudioStreamDemo>`: el backend lo invoca desde la IRQ con el contexto.
	static void audio_service(AudioStreamDemo& c, eng::u16 v) { c.on_audio_irq(v); }

	/// **IRQ de audio (nivel 4)**: avanza el buffer de reproduccion y reprograma el puntero de
	/// la voz. No descomprime (eso se hace en el bucle principal).
	void on_audio_irq(eng::u16) {
		m_irq = static_cast<eng::u8>(m_irq + 1u);
		if (m_stream.advance()) {
			m_paula.set_buffer(kChannel, m_stream.play_pcm(), kChunkSamples / 2u);
			m_swap = static_cast<eng::u8>(m_swap + 1u);
		} else {
			m_underrun = static_cast<eng::u8>(m_underrun + 1u);
		}
	}

	/// Rellena todos los buffers libres con el siguiente chunk de la melodia.
	void refill() {
		while (m_stream.needs_data()) {
			const eng::u8 idx = m_stream.first_free();
			if (idx >= kNumBuffers) {
				break;
			}
			const eng::s32 n =
				synth_chunk(static_cast<eng::u8>(m_stream.next_chunk() % kMelodyChunks));
			if (n <= 0 ||
			    !m_stream.provide(idx, eng::Span<const eng::u8>(m_comp.view.data(),
									     static_cast<eng::usize>(n)))) {
				break;
			}
		}
	}

	/// Sintetiza el chunk `c` (onda cuadrada) y lo comprime con el codec real. Devuelve el
	/// tamano comprimido, o -1 si no cabe.
	eng::s32 synth_chunk(eng::u8 c) {
		const eng::u32 half = kHalf[c];
		for (eng::u32 i = 0; i < kChunkSamples; ++i) {
			g_scratch[i] = ((i / half) & 1u) != 0u ? eng::u8{28} : eng::u8{static_cast<eng::u8>(256u - 28u)};
		}
		return eng::audio::pcm_codec::encode(
			eng::Span<const eng::u8>(g_scratch, kChunkSamples),
			eng::Span<eng::u8>(m_comp.view.data(), kMaxComp));
	}

	/// Barra de progreso (plano 0) que avanza con el chunk en curso: evidencia visual del flujo.
	void draw_bar() {
		eng::u8* row = m_bitplane_block.view.data() + static_cast<eng::u32>(kBarRow) * kBytesPerRow;
		const eng::u32 frac = static_cast<eng::u32>(m_stream.next_chunk() % kMelodyChunks);
		const eng::u32 w = ((frac + 1u) * kBytesPerRow) / kMelodyChunks;
		for (eng::u32 b = 0; b < kBytesPerRow; ++b) {
			row[b] = b < w ? 0xffu : 0x00u;
		}
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched {m_copper_block};
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x1200, 1u,
					  m_bitplane_block.view, kPlaneBytes);
		sched.move(eng::copper::color_register(0), 0x001u);
		sched.move(eng::copper::color_register(1), 0x00fu);
		sched.end();
		m_copper_ptr = sched.data();
		return sched.ok();
	}

	bool m_init_ok = false;
	// Contadores de 8 bits `volatile` compartidos con la IRQ: el 68000 lee/escribe un byte de
	// forma atomica (un `u32` se parte en dos accesos al bus y la lectura se desgarra).
	volatile eng::u8 m_irq = 0;
	volatile eng::u8 m_swap = 0;
	volatile eng::u8 m_underrun = 0;
	const eng::u16* m_copper_ptr = nullptr;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::AudioTag> m_pcm0 {};
	eng::Block<eng::AudioTag> m_pcm1 {};
	eng::Block<eng::AudioTag> m_comp {};
	eng::audio::PcmStream<kNumBuffers> m_stream {};
	eng::amiga::PaulaAudio m_paula {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	AudioStreamDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
