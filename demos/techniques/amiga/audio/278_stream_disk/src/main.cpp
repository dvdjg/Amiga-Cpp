// ============================================================================
// Demo 278: streaming de audio comprimido DESDE DISCO — AUZX + media + PcmStream.
// ============================================================================
//
// Camino completo de A5: lee del `DH1:` un fichero **AUZX** (cabecera + chunks comprimidos con
// Fibonacci Delta), lo reconoce con `eng::audio::media`, lo streamea con `PcmStream` (la CPU
// descomprime cada chunk directo a un buffer de Chip) y Paula lo reproduce por DMA; la IRQ de
// audio solo cambia el puntero. El fichero lo genera el pipeline de PC:
//
//   node tools/audio/gen-melody.mjs out/tmp/melody.raw            (melodia de dominio publico)
//   host-tools/pack-pcm out/tmp/melody.raw out/tmp/melody.auzx fib 8000 1024
//   node tools/fs/make-volume.mjs --add out/tmp/melody.auzx:data/audio/melody.auzx
//
// La lectura se hace en `init` (sin `takeover_display`, asi que dos.library sigue viva). Informa
// `mark_ready` con `detail = (irq << 16) | swaps`; `irq == swaps` y sin underruns = OK.
//
// Build/run:
//   bash tools/build/build-demo.sh demos/techniques/amiga/audio/278_stream_disk --release --clean
//   bash tools/run/run-demo.sh demos/techniques/amiga/audio/278_stream_disk --warp

#include <eng/api/api.hpp>
#include <eng/audio/media.hpp>
#include <eng/audio/pcm_stream.hpp>
#include <eng/os/file.hpp>
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

constexpr eng::usize kMaxFile = 64u * 1024u; ///< tamano maximo del AUZX en disco
constexpr eng::usize kMaxChunk = 2048u;      ///< muestras PCM por chunk (buffer de Chip)
constexpr eng::u8 kNumBuffers = 3u;          ///< **triple buffer** (mas margen ante seeks)
constexpr eng::u8 kChannel = 3u;
constexpr eng::u8 kVolume = 48u;
constexpr const char* kFilePath = "data/audio/melody.auzx";
constexpr eng::u32 kFrameReport = 240u; ///< ~4 s a 50 Hz

struct StreamDiskDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({96u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027801u);
			return;
		}
		m_pcm0 = backend.memory().chip.allocate_block<eng::AudioTag>(kMaxChunk, 4);
		m_pcm1 = backend.memory().chip.allocate_block<eng::AudioTag>(kMaxChunk, 4);
		m_pcm2 = backend.memory().chip.allocate_block<eng::AudioTag>(kMaxChunk, 4);
		m_file = backend.memory().chip.allocate_block<eng::AudioTag>(kMaxFile, 4);
		if (!m_pcm0.valid() || !m_pcm1.valid() || !m_pcm2.valid() || !m_file.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027802u);
			return;
		}

		// Lee el AUZX de disco (dos.library viva: sin takeover). Sin el fichero, falla claro.
		const eng::os::FileHandle h = eng::os::file_open(kFilePath, eng::os::FileMode::Read);
		if (h == 0u) {
			eng::debug::mark_failed(g_eng_run_status, 0x0002780Au);
			return;
		}
		const eng::s32 n = eng::os::file_read_sync(
		    h, eng::Span<eng::u8> {m_file.view.data(), kMaxFile}, 0u);
		eng::os::file_close(h);
		if (n <= 0) {
			eng::debug::mark_failed(g_eng_run_status, 0x0002780Bu);
			return;
		}
		m_blob = eng::Span<const eng::u8> {m_file.view.data(), static_cast<eng::usize>(n)};

		// Reconoce el medio (contenedor + codec + chunks) con la interfaz unica.
		if (!eng::audio::media::open(m_blob, m_info)) {
			eng::debug::mark_failed(g_eng_run_status, 0x0002780Cu);
			return;
		}
		const eng::u16 chunk = m_info.chunk_samples;
		if (chunk == 0u || chunk > kMaxChunk || m_info.num_chunks == 0u) {
			eng::debug::mark_failed(g_eng_run_status, 0x0002780Du);
			return;
		}

		eng::audio::PcmStream<kNumBuffers>::Config cfg {
			m_info.sample_rate, chunk, m_info.num_chunks,
			static_cast<eng::u8>(m_info.codec)};
		eng::Span<eng::u8> bufs[kNumBuffers] = {
			eng::Span<eng::u8>(m_pcm0.view.data(), chunk),
			eng::Span<eng::u8>(m_pcm1.view.data(), chunk),
			eng::Span<eng::u8>(m_pcm2.view.data(), chunk)};
		m_stream.begin(cfg, bufs);
		// **Seek**: empieza por un chunk del indice AUZX (salta la primera mitad sin leerla).
		m_stream.seek(static_cast<eng::u16>(m_info.num_chunks / 2u));

		if (!backend.set_audio_service(&StreamDiskDemo::audio_service, *this)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00027803u);
			return;
		}
		refill();
		m_paula.set_period(kChannel, eng::audio::paula::period_for_hz(m_info.sample_rate));
		m_paula.set_volume(kChannel, kVolume);
		m_paula.set_buffer(kChannel, m_stream.play_pcm(), chunk / 2u);
		m_paula.start_channel(kChannel);
		m_init_ok = true;
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		if (!m_init_ok) {
			return;
		}
		refill();
		if (context.frame.frame_index == kFrameReport) {
			if (m_irq > 0u && m_swap > 0u && m_underrun == 0u) {
				eng::debug::mark_ready(g_eng_run_status,
						       (static_cast<eng::u32>(m_irq) << 16u) | m_swap);
			} else {
				eng::debug::mark_failed(
				    g_eng_run_status,
				    (static_cast<eng::u32>(m_irq) << 16u) |
					(static_cast<eng::u32>(m_swap) << 8u) | m_underrun);
			}
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		auto& d = backend.debug();
		d.clear();
		d.text(40, 40, "278: streaming AUZX desde DH1:data/audio/melody.auzx (FibDelta)",
		       0x00ffffff);
		eng::debug::probe_when_ready(g_eng_run_status, 0u);
	}

private:
	static void audio_service(StreamDiskDemo& c, eng::u16 v) { c.on_audio_irq(v); }

	/// **IRQ de audio (nivel 4)**: avanza el buffer y reprograma el puntero. No descomprime.
	void on_audio_irq(eng::u16) {
		++m_irq;
		if (m_stream.advance()) {
			m_paula.set_buffer(kChannel, m_stream.play_pcm(), m_info.chunk_samples / 2u);
			++m_swap;
		} else if (!m_stream.finished()) {
			++m_underrun; // `false` al final del stream es fin normal, no underrun
		}
	}

	/// Descomprime el siguiente chunk de la melodia al buffer libre (en bucle).
	void refill() {
		while (m_stream.needs_data()) {
			const eng::u8 idx = m_stream.first_free();
			if (idx >= kNumBuffers) {
				break;
			}
			const eng::u16 c = static_cast<eng::u16>(m_stream.next_chunk() % m_info.num_chunks);
			eng::u32 sz = 0u;
			const eng::Span<const eng::u8> body =
			    eng::audio::media::chunk_data(m_blob, m_info, c, sz);
			if (sz == 0u || !m_stream.provide(idx, body)) {
				++m_underrun;
				break;
			}
		}
	}

	eng::audio::media::Info m_info {};
	eng::Span<const eng::u8> m_blob {};
	eng::audio::PcmStream<kNumBuffers> m_stream {};
	eng::amiga::PaulaAudio m_paula {};
	// Buffers DMA de Paula: el banco (Chip) va en el tipo, así no pueden acabar en Slow/Fast.
	eng::Block<eng::AudioTag, eng::MemoryKind::Chip> m_pcm0 {}, m_pcm1 {}, m_pcm2 {}, m_file {};
	// Contadores de 8 bits `volatile` compartidos con la IRQ (una lectura de `u32` se desgarra).
	volatile eng::u8 m_irq = 0u;
	volatile eng::u8 m_swap = 0u;
	volatile eng::u8 m_underrun = 0u;
	bool m_init_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	static StreamDiskDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);
	return 0;
}
