#pragma once

/// \file acp1_stream.hpp
/// Adaptador cooperativo entre una composición ACP1 y buffers PCM listos para Paula.

#include <eng/audio/audio.hpp>
#include <eng/audio/media.hpp>
#include <eng/core/types/span.hpp>
#include <eng/os/stream.hpp>

namespace eng::audio {

/// Stream PCM mezclado desde ACP1; la tarea cooperativa llama `refill`, la IRQ solo `advance`.
template <eng::u8 NumBuffers>
class Acp1Stream {
	static_assert(NumBuffers >= 2u && NumBuffers <= 8u, "Acp1Stream: 2..8 buffers");

public:
	/// Inicializa el timeline y los buffers PCM prestados por el backend; el blob ACP1 debe seguir vivo.
	[[nodiscard]] bool begin(eng::Span<const eng::u8> blob, const media::Info& media_info,
		const eng::Span<eng::u8>(&buffers)[NumBuffers], eng::Span<eng::u8> scratch,
		eng::Span<eng::s16> accumulator, eng::u16 chunk_samples) noexcept {
		if (media_info.container != media::Container::Acp1 || chunk_samples == 0u ||
			(chunk_samples & 1u) != 0u || scratch.empty() || accumulator.size() < chunk_samples) return false;
		m_blob = blob;
		m_media = media_info;
		m_chunk_samples = chunk_samples;
		const eng::u32 chunk_count = (media_info.total_samples + chunk_samples - 1u) / chunk_samples;
		if (chunk_count > 65535u) return false;
		m_num_chunks = static_cast<eng::u16>(chunk_count);
		m_next_chunk = 0u;
		m_scratch = scratch;
		m_accumulator = accumulator;
		for (eng::u8 i = 0u; i < NumBuffers; ++i) {
			if (buffers[i].size() < chunk_samples) return false;
			m_pcm[i] = buffers[i].subspan(0u, chunk_samples);
		}
		m_state = eng::os::ChunkStream<NumBuffers> {};
		m_started = true;
		m_exhausted = false;
		return true;
	}

	/// Llena todos los buffers libres que quepan según el presupuesto de CPU del llamador.
	[[nodiscard]] eng::u8 refill(eng::u8 max_chunks = NumBuffers) noexcept {
		if (!m_started) return 0u;
		eng::u8 filled = 0u;
		while (filled < max_chunks && m_next_chunk < m_num_chunks && m_state.needs_data()) {
			const eng::u8 index = first_free();
			if (index >= NumBuffers) break;
			const eng::u32 first = static_cast<eng::u32>(m_next_chunk) * m_chunk_samples;
			const eng::usize count = m_media.total_samples - first < m_chunk_samples
				? static_cast<eng::usize>(m_media.total_samples - first) : m_chunk_samples;
			const eng::s32 mixed = media::mix_window(m_blob, m_media, first,
				{m_pcm[index].data(), count}, m_scratch, m_accumulator);
			if (mixed != static_cast<eng::s32>(count)) {
				m_failed = true;
				return filled;
			}
			for (eng::usize i = count; i < m_chunk_samples; ++i) m_pcm[index][i] = 0u;
			++m_next_chunk;
			++filled;
			if (!m_state.on_chunk_ready(index)) { m_failed = true; return filled; }
			if (m_next_chunk >= m_num_chunks) { m_exhausted = true; m_state.set_eof(); }
		}
		return filled;
	}

	/// Avanza al siguiente buffer; se invoca desde la IRQ sin decodificar ni mezclar.
	[[nodiscard]] bool advance() noexcept { return m_state.advance(); }
	/// Distingue el final normal del stream de un underrun de alimentación.
	[[nodiscard]] bool at_end() const noexcept { return m_state.eof() && !m_state.play_ready(); }
	/// Señala que el servicio cooperativo encontró datos o estado inválidos.
	[[nodiscard]] bool failed() const noexcept { return m_failed; }
	/// Indica si todos los chunks fueron consumidos.
	[[nodiscard]] bool finished() const noexcept { return m_state.finished(); }
	/// Indica si el ACP1 llegó a su último chunk.
	[[nodiscard]] bool eof() const noexcept { return m_state.eof(); }
	/// Máscara de buffers que el servicio cooperativo debe rellenar.
	[[nodiscard]] eng::u8 free_mask() const noexcept { return m_state.request_mask(); }
	/// Vista de reproducción PCM vigente; su memoria pertenece al llamador y debe ser Chip RAM en Amiga.
	[[nodiscard]] eng::Span<const eng::u8> play_pcm() const noexcept { return m_pcm[m_state.play_index()]; }
	/// Rellena un chunk ya liberado por la IRQ, usado por la variante que mezcla pistas en vBlank.
	[[nodiscard]] bool render_next(eng::Span<eng::u8> output) noexcept {
		if (!m_started || m_next_chunk >= m_num_chunks || output.size() < m_chunk_samples) return false;
		const eng::u32 first = static_cast<eng::u32>(m_next_chunk) * m_chunk_samples;
		const eng::usize count = m_media.total_samples - first < m_chunk_samples
			? static_cast<eng::usize>(m_media.total_samples - first) : m_chunk_samples;
		const eng::s32 mixed = media::mix_window(m_blob, m_media, first,
			{output.data(), count}, m_scratch, m_accumulator);
		if (mixed != static_cast<eng::s32>(count)) { m_failed = true; return false; }
		for (eng::usize i = count; i < m_chunk_samples; ++i) output[i] = 0u;
		++m_next_chunk;
		if (m_next_chunk >= m_num_chunks) m_exhausted = true;
		return true;
	}
	/// Publica como listo el buffer rellenado por `render_next`; no se llama desde la IRQ.
	[[nodiscard]] bool mark_ready(eng::u8 index) noexcept {
		return index < NumBuffers && m_state.on_chunk_ready(index);
	}
	/// Indica si hay al menos un buffer que pueda rellenarse.
	[[nodiscard]] bool needs_data() const noexcept { return m_state.needs_data(); }
	/// Declara EOF cuando el productor cooperativo ya ha publicado todos los chunks.
	void finish_input() noexcept { if (m_exhausted) m_state.set_eof(); }
	/// Índice del primer buffer libre para `render_next`.
	[[nodiscard]] eng::u8 first_free_buffer() const noexcept { return first_free(); }
	/// Próximo índice de timeline que `refill` decodificará.
	[[nodiscard]] eng::u16 next_chunk() const noexcept { return m_next_chunk; }
	/// Chunks PCM de tamaño fijo, con el último rellenado a silencio.
	[[nodiscard]] eng::u16 num_chunks() const noexcept { return m_num_chunks; }
	/// Tamaño PCM por buffer en muestras y bytes.
	[[nodiscard]] eng::u16 chunk_samples() const noexcept { return m_chunk_samples; }

private:
	/// Encuentra el primer buffer libre del ChunkStream.
	[[nodiscard]] eng::u8 first_free() const noexcept {
		const eng::u8 mask = m_state.request_mask();
		for (eng::u8 i = 0u; i < NumBuffers; ++i) if ((mask & (1u << i)) != 0u) return i;
		return NumBuffers;
	}

	eng::Span<const eng::u8> m_blob {}; ///< Blob ACP1 no propietario.
	media::Info m_media {}; ///< Estructura ACP1 ya validada.
	eng::os::ChunkStream<NumBuffers> m_state {}; ///< Estado compartido por tarea e IRQ.
	eng::Span<eng::u8> m_pcm[NumBuffers] {}; ///< Buffers de salida prestados; Chip RAM en el backend Amiga.
	eng::Span<eng::u8> m_scratch {}; ///< Scratch de un chunk decodificado por track.
	eng::Span<eng::s16> m_accumulator {}; ///< Acumulador PCM para mezclar las pistas.
	eng::u16 m_chunk_samples = 0u; ///< Tamaño fijo de cada buffer PCM.
	eng::u16 m_num_chunks = 0u; ///< Chunks que cubren la duración ACP1.
	eng::u16 m_next_chunk = 0u; ///< Cursor de timeline, propiedad de la tarea cooperativa.
	volatile bool m_failed = false; ///< Error de decode/mix compartido entre productor y lector de estado.
	bool m_exhausted = false; ///< El productor ya preparó todos los chunks del ACP1.
	bool m_started = false; ///< begin completado.
};

} // namespace eng::audio
