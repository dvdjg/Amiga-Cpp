// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/audio/272_audio_stream --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/audio/272_audio_stream --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/audio/272_audio_stream --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/audio/272_audio_stream --keep-running

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
// **Estado: VERIFICADA.** Informa `mark_ready` en el frame `kFrameReport` con
// `detail = (irq << 16) | swaps`; medido `irq == swaps` (0 underruns).
//
// Dos fuentes: si existe `data/audio/tone_8k_512k.raw` (M8) se **precarga de disco** en Chip
// (`load_pcm_from_disk`, `Codec::None`, 8 kHz) y se streamea; si no, se **pre-sintetiza y
// pre-codifica la melodia una sola vez** (`m_enc`, DeltaRle, 16 kHz). En ambos casos el feeder por
// frame solo copia datos ya listos: sintetizar+codificar por frame hundia el ritmo (underruns).
// La lectura de disco va **antes** de `takeover_display` porque este apaga todo el DMA y congela
// las IRQs del sistema (dos/trackdisk necesitan DMA+IRQ). Analisis y log del emulador:
// docs/debugging/investigaciones/audio-stream-irq-rate.md.
// Ver tambien docs/engine/architecture/AUDIO_STREAMING.md y ROADMAP_AUDIO.md (A5).
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/audio/audio_mode.hpp>
#include <eng/audio/pcm_codec.hpp>
#include <eng/audio/pcm_stream.hpp>
#include <eng/os/file.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/paula.hpp>

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
constexpr eng::u16 kFileRateHz = 8000u;  ///< `tone_8k_512k.raw` (mono 8-bit)
constexpr eng::u8 kPreloadChunks = 32u;  ///< chunks PCM precargados de disco (M8)
constexpr const char* kFilePath = "data/audio/tone_8k_512k.raw";
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
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 192u * 1024u, 32u * 1024u, 8u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027201u);
			return;
		}
		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kPlaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(1024, 16);
		m_pcm0 = backend.memory().chip.allocate_block<eng::AudioTag>(kChunkSamples, 4);
		m_pcm1 = backend.memory().chip.allocate_block<eng::AudioTag>(kChunkSamples, 4);
		m_comp = backend.memory().chip.allocate_block<eng::AudioTag>(kMaxComp, 4);
		m_enc = backend.memory().chip.allocate_block<eng::AudioTag>(kMelodyChunks * kMaxComp, 4);
		m_file = backend.memory().chip.allocate_block<eng::AudioTag>(
			static_cast<eng::u32>(kPreloadChunks) * kChunkSamples, 4);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_pcm0.valid() ||
		    !m_pcm1.valid() || !m_comp.valid() || !m_enc.valid() || !m_file.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027202u);
			return;
		}

		// M8: leer el PCM de disco **antes** del takeover. `takeover_display` apaga TODO el DMA
		// (DMACON=dma_clear_all) y congela las IRQs del sistema, asi que despues la E/S de disco
		// (dos/trackdisk, que necesitan DMA+IRQ) se cuelga. Se precargan `kPreloadChunks` chunks.
		load_pcm_from_disk();

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027203u);
			return;
		}
		backend.takeover_display(m_copper_ptr);

		// Stream: 2 buffers PCM del llamador (Chip). En modo fichero el PCM ya viene crudo
		// (`Codec::None`); en modo sintetizado, la melodia va comprimida (`DeltaRle`).
		eng::audio::PcmStream<kNumBuffers>::Config cfg {
			static_cast<eng::u16>(m_file_chunks > 0u ? kFileRateHz : kRateHz), kChunkSamples,
			kStreamChunks,
			static_cast<eng::u8>(m_file_chunks > 0u ? eng::audio::pcm_codec::Codec::None
								: eng::audio::pcm_codec::Codec::DeltaRle)};
		eng::Span<eng::u8> bufs[kNumBuffers] = {
			eng::Span<eng::u8>(m_pcm0.view.data(), kChunkSamples),
			eng::Span<eng::u8>(m_pcm1.view.data(), kChunkSamples)};
		m_stream.begin(cfg, bufs);

		// En modo sintetizado, pre-sintetiza y pre-codifica la melodia UNA sola vez (8 chunks).
		// El feeder por frame solo copia el chunk ya codificado: sintetizar+codificar 2048
		// muestras por frame hundia el ritmo y provocaba *underruns* (audio-stream-irq-rate.md).
		if (m_file_chunks == 0u) {
			for (eng::u8 c = 0u; c < kMelodyChunks; ++c) {
				const eng::s32 n = synth_chunk(c);
				if (n <= 0) {
					eng::debug::mark_failed(g_eng_run_status, 0x00027205u);
					return;
				}
				eng::u8* dst = m_enc.view.data() + static_cast<eng::u32>(c) * kMaxComp;
				for (eng::s32 i = 0; i < n; ++i) {
					dst[i] = m_comp.view.data()[i];
				}
				m_enc_len[c] = static_cast<eng::u32>(n);
			}
		}

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

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
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

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
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

	/// M8: precarga el PCM de disco en Chip **antes** del takeover. Deja `m_file_chunks` con el
	/// numero de chunks completos leidos (0 = no hay fichero -> se sintetiza).
	void load_pcm_from_disk() {
		m_file_chunks = 0u;
		const eng::os::FileHandle h = eng::os::file_open(kFilePath, eng::os::FileMode::Read);
		if (h == 0u) {
			return;
		}
		const eng::u32 want = static_cast<eng::u32>(kPreloadChunks) * kChunkSamples;
		const eng::s32 n = eng::os::file_read_sync(
			h, eng::Span<eng::u8> { m_file.view.data(), want }, 0u);
		eng::os::file_close(h);
		if (n > 0) {
			m_file_chunks = static_cast<eng::u8>(static_cast<eng::u32>(n) / kChunkSamples);
		}
	}

	/// Rellena todos los buffers libres con el siguiente chunk de la melodia.
	void refill() {
		while (m_stream.needs_data()) {
			const eng::u8 idx = m_stream.first_free();
			if (idx >= kNumBuffers) {
				break;
			}
			if (m_file_chunks > 0u) {
				// Modo fichero: PCM crudo precargado; se recorre en bucle.
				const eng::u8 c = static_cast<eng::u8>(m_stream.next_chunk() % m_file_chunks);
				if (!m_stream.provide(idx, eng::Span<const eng::u8>(
							 m_file.view.data() +
								 static_cast<eng::u32>(c) * kChunkSamples,
							 kChunkSamples))) {
					break;
				}
				continue;
			}
			const eng::u8 c = static_cast<eng::u8>(m_stream.next_chunk() % kMelodyChunks);
			const eng::u32 n = m_enc_len[c];
			if (n == 0u ||
			    !m_stream.provide(idx, eng::Span<const eng::u8>(
						       m_enc.view.data() + static_cast<eng::u32>(c) * kMaxComp,
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
		// Paleta con blanco/verde/amarillo (gate visual de la regresion): el fondo (color 0) va
		// blanco arriba, verde a la altura de la barra y amarillo abajo, cambiando por Copper. La
		// barra de progreso (bit 1 del plano) usa color 1.
		sched.move(eng::copper::color_register(0), 0xfffu);
		sched.move(eng::copper::color_register(1), 0x00fu);
		sched.wait_line(kBarRow);
		sched.move(eng::copper::color_register(0), 0x0f0u);
		sched.wait_line(static_cast<eng::u8>(kBarRow + 20u));
		sched.move(eng::copper::color_register(0), 0xff0u);
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
	eng::Block<eng::AudioTag> m_enc {}; ///< melodia pre-codificada (kMelodyChunks * kMaxComp)
	eng::u32 m_enc_len[kMelodyChunks] {}; ///< tamano codificado de cada chunk de la melodia
	eng::Block<eng::AudioTag> m_file {}; ///< PCM de disco precargado (M8)
	eng::u8 m_file_chunks = 0u;          ///< chunks PCM validos en `m_file` (0 = sintetizar)
	eng::audio::PcmStream<kNumBuffers> m_stream {};
	eng::amiga::PaulaAudio m_paula {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	AudioStreamDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
