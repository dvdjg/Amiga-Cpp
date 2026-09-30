// Build/run:
//   bash tools/build/build-demo.sh demos/techniques/amiga/audio/280_acp1_stream --release --clean
//   bash tools/run/run-demo.sh demos/techniques/amiga/audio/280_acp1_stream --warp

#include <eng/api/api.hpp>
#include <eng/audio/acp1_stream.hpp>
#include <eng/audio/audio_feeder.hpp>
#include <eng/audio/media.hpp>
#include <eng/os/file.hpp>
#include <eng/platform/amiga/backend.hpp>

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

constexpr eng::usize kMaxFile = 128u * 1024u;
constexpr eng::u16 kChunkSamples = 2048u;
constexpr eng::u8 kBuffers = 3u;
constexpr eng::u8 kChannel = 3u;
constexpr eng::u8 kVolume = 48u;
constexpr eng::u16 kSampleRate = 22050u;
constexpr eng::u32 kFrameReport = 240u;
constexpr const char* kPath = "data/audio/theme.acp1";

struct Acp1Demo {
	/// Lee/valida ACP1 y reserva los buffers de reproducción antes de instalar su IRQ exclusiva.
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({192u * 1024u, 0u, 0u, 0u})) { fail(0x00028001u); return; }
		m_file = backend.memory_manager().chip().reserve<eng::AudioTag>(kMaxFile, 4u);
		m_pcm[0] = backend.memory_manager().chip().reserve<eng::AudioTag>(kChunkSamples, 4u);
		m_pcm[1] = backend.memory_manager().chip().reserve<eng::AudioTag>(kChunkSamples, 4u);
		m_pcm[2] = backend.memory_manager().chip().reserve<eng::AudioTag>(kChunkSamples, 4u);
		m_scratch = backend.memory_manager().chip().reserve<eng::AudioTag>(kChunkSamples, 4u);
		m_accumulator = backend.memory_manager().chip().reserve<eng::AudioTag>(kChunkSamples * sizeof(eng::s16), 4u);
		if (!m_file.valid() || !m_pcm[0].valid() || !m_pcm[1].valid() || !m_pcm[2].valid() ||
			!m_scratch.valid() || !m_accumulator.valid()) { fail(0x00028002u); return; }
		const eng::os::FileHandle handle = eng::os::file_open(kPath, eng::os::FileMode::Read);
		if (handle == 0u) { fail(0x00028003u); return; }
		const eng::s32 file_bytes = eng::os::file_read_sync(handle, {m_file.data(), kMaxFile}, 0u);
		eng::os::file_close(handle);
		if (file_bytes <= 0) { fail(0x00028004u); return; }
		m_blob = {m_file.data(), static_cast<eng::usize>(file_bytes)};
		if (!eng::audio::media::open(m_blob, m_media) || m_media.container != eng::audio::media::Container::Acp1 ||
			m_media.sample_rate != kSampleRate || m_media.total_samples == 0u) { fail(0x00028005u); return; }
		eng::Span<eng::u8> pcm[kBuffers] {
			{m_pcm[0].data(), kChunkSamples}, {m_pcm[1].data(), kChunkSamples}, {m_pcm[2].data(), kChunkSamples}};
		const eng::Span<eng::s16> accumulator {reinterpret_cast<eng::s16*>(m_accumulator.data()), kChunkSamples};
		if (!m_stream.begin(m_blob, m_media, pcm, {m_scratch.data(), kChunkSamples}, accumulator, kChunkSamples)) {
			fail(0x00028006u); return;
		}
		if (!backend.composition_playback_begin()) { fail(0x00028007u); return; }
		if (!backend.set_composition_audio_service(&Acp1Demo::audio_irq, *this)) {
			backend.composition_playback_end(); fail(0x00028008u); return;
		}
		if (m_stream.refill() == 0u || m_stream.failed()) {
			backend.clear_composition_audio_service();
			backend.composition_playback_end();
			fail(0x00028009u); return;
		}
		m_stream.finish_input();
		m_backend = &backend;
		backend.start_audio_buffer(kChannel, m_stream.play_pcm().data(), kChunkSamples / 2u,
			eng::audio::paula::period_for_hz(kSampleRate), kVolume);
		m_active = true;
	}

	/// Mezcla como máximo un chunk por frame y publica READY cuando la reproducción ya ha swapeado.
	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_active) return;
		refill_free_buffers();
		if (m_stream.failed()) { stop(backend); fail(0x0002800au); return; }
		if (context.frame.frame_index == kFrameReport) {
			if (m_feeder.swap_count() > 0u && m_feeder.underrun_count() == 0u)
				eng::debug::mark_ready(g_eng_run_status, m_feeder.detail());
			else fail(0x0002800bu);
		}
		if (m_stream.finished()) {
			stop(backend);
		}
	}

	/// Muestra el estado de la reproducción sin instalar otro uso del vector de audio.
	void render(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		auto& debug = backend.debug();
		debug.clear();
		debug.text(32, 32, "280: ACP1 v2, eventos mezclados fuera de la IRQ", 0x00ffffffu);
		eng::debug::probe_when_ready(g_eng_run_status, 0u);
	}

private:
	/// Trampolín requerido por `AmigaBackend::set_composition_audio_service`.
	static void audio_irq(Acp1Demo& self, eng::u16) { self.on_audio_irq(); }

	/// Avanza el feeder y cambia solo el puntero/longitud PCM; la mezcla queda fuera de la ISR.
	void on_audio_irq() {
		if (m_feeder.on_irq()) m_backend->swap_audio_buffer(kChannel, m_stream.play_pcm().data(), kChunkSamples / 2u);
	}

	/// Mezcla únicamente buffers ya liberados por Paula; la última región se completa con silencio.
	void refill_free_buffers() {
		while (m_stream.needs_data()) {
			const eng::u8 index = m_stream.first_free_buffer();
			if (index >= 3u) return;
			if (!m_stream.render_next({m_pcm[index].data(), kChunkSamples}) || !m_stream.mark_ready(index)) return;
		}
		m_stream.finish_input();
	}

	/// Marca fallo para que el analizador reciba un detalle estable.
	void fail(eng::u32 code) { eng::debug::mark_failed(g_eng_run_status, code); }

	/// Detiene DMA y retira el servicio antes de cualquier liberación o nueva reproducción.
	void stop(eng::amiga::AmigaBackend& backend) {
		if (!m_active) return;
		backend.stop_composition_audio(kChannel);
		backend.clear_composition_audio_service();
		backend.composition_playback_end();
		m_active = false;
	}

	eng::amiga::AmigaBackend* m_backend = nullptr;
	eng::Span<const eng::u8> m_blob {};
	eng::audio::media::Info m_media {};
	eng::audio::Acp1Stream<kBuffers> m_stream {};
	eng::audio::AudioFeeder<eng::audio::Acp1Stream<kBuffers>> m_feeder {m_stream};
	eng::Block<eng::AudioTag, eng::MemoryKind::Chip> m_file {};
	eng::Block<eng::AudioTag, eng::MemoryKind::Chip> m_pcm[kBuffers] {};
	eng::Block<eng::AudioTag, eng::MemoryKind::Chip> m_scratch {};
	eng::Block<eng::AudioTag, eng::MemoryKind::Chip> m_accumulator {};
	bool m_active = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	static Acp1Demo game {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffffu);
	return 0;
}
