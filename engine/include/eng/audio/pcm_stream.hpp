#pragma once

/// \file pcm_stream.hpp
/// **Streaming de PCM desde disquete** (`eng::audio`): une la máquina de estados de buffers
/// (`eng::os::ChunkStream`) con el descompresor (`pcm_codec`) para reproducir una grabación
/// digital con **footprint = N buffers**, no el archivo entero. Mientras Paula reproduce un buffer
/// PCM, el disco llena el siguiente y la CPU descomprime el libre. Ver
/// `docs/engine/architecture/AUDIO_STREAMING.md`.
///
/// Reparto de responsabilidades (el flujo es puro; el hardware lo pone el backend):
///   - **E/S**: el llamador lee el chunk comprimido (E/S asíncrona del mini-SO) y lo entrega con
///     `provide`; los buffers PCM son memoria **Chip** del llamador (Paula los lee por DMA).
///   - **IRQ de audio (nivel 4)**: solo llama a `advance()` y reprograma el puntero de Paula con
///     `play_pcm()`; **no** descomprime.
///   - **Tarea de fondo / bucle**: mientras `needs_data()`, lee `next_chunk()` en un buffer
///     `first_free()` y lo entrega con `provide`.
///
/// La cabecera de archivo (`AUZX`, `compression`) la valida el llamador; aquí llega ya en `Config`.

#include <eng/audio/pcm_codec.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/os/stream.hpp>

namespace eng::audio {

/// Stream PCM con `NumBuffers` buffers (2 o 3) tipo `ChunkStream`.
template <eng::u8 NumBuffers>
class PcmStream {
	static_assert(NumBuffers >= 2u && NumBuffers <= 8u, "PcmStream: 2..8 buffers");

public:
	static constexpr eng::u8 kNumBuffers = NumBuffers;

	/// Config del stream (cabecera AUZX ya validada).
	struct Config {
		eng::u16 sample_rate = 0;   ///< Hz (`AUDxPER = period_for_hz(sample_rate)`)
		eng::u16 chunk_samples = 0; ///< muestras PCM por chunk (descomprimido)
		eng::u16 num_chunks = 0;    ///< número total de chunks del archivo
		eng::u8 compression = 0;    ///< `pcm_codec::Codec`
	};

	/// Arranca el stream con `cfg` y los `NumBuffers` buffers PCM (Chip RAM del llamador, de
	/// `cfg.chunk_samples` bytes cada uno). Deja todos los buffers libres para cargar los primeros.
	void begin(const Config& cfg, const eng::Span<eng::u8>(&pcm)[NumBuffers]) noexcept {
		m_cfg = cfg;
		for (eng::u8 i = 0; i < NumBuffers; ++i) {
			m_pcm[i] = pcm[i];
		}
		m_state = eng::os::ChunkStream<NumBuffers> {};
		m_next = 0;
	}

	[[nodiscard]] const Config& config() const noexcept { return m_cfg; }

	/// **Estado de buffers** (`ChunkStream`) para que un `FileChunkFeeder` lo alimente directamente:
	/// con `Codec::None` el feeder escribe el PCM crudo en el propio buffer (sin `provide`/decode).
	/// Ver `AUDIO_STREAMING.md` §3 y `FileChunkFeeder` (`eng/os/file_stream.hpp`).
	[[nodiscard]] eng::os::ChunkStream<NumBuffers>& state() noexcept { return m_state; }

	/// Máscara de buffers **libres** (0 si no hay trabajo o ya es EOF).
	[[nodiscard]] eng::u8 free_mask() const noexcept { return m_state.request_mask(); }
	[[nodiscard]] bool needs_data() const noexcept { return m_state.needs_data(); }

	/// Índice del chunk que toca leer para el próximo `provide`.
	[[nodiscard]] eng::u16 next_chunk() const noexcept { return m_next; }

	/// **Reposiciona** el stream en el chunk `chunk` (se acota a `[0, num_chunks)`) y reinicia el
	/// estado de buffers: útil para *seek*/bucle en streams largos, aprovechando el índice de
	/// chunks del AUZX (saltar sin leer los anteriores). Tras `seek` el llamador debe volver a
	/// rellenar los buffers con `provide` antes de reanudar el DMA.
	void seek(eng::u16 chunk) noexcept {
		m_state = eng::os::ChunkStream<NumBuffers> {};
		m_next = (chunk < m_cfg.num_chunks) ? chunk : 0u;
	}

	/// Índice del primer buffer libre (para `provide`), o `NumBuffers` si no hay.
	[[nodiscard]] eng::u8 first_free() const noexcept {
		const eng::u8 m = free_mask();
		for (eng::u8 i = 0; i < NumBuffers; ++i) {
			if ((m & (1u << i)) != 0u) {
				return i;
			}
		}
		return NumBuffers;
	}

	/// Decodifica el chunk `next_chunk()` (ya leído por el llamador) en el buffer `idx` y lo marca
	/// listo. Devuelve `false` si `idx` no estaba libre, el flujo es inválido o no produce
	/// exactamente `chunk_samples` muestras (en ese caso el buffer queda como estaba).
	bool provide(eng::u8 idx, eng::Span<const eng::u8> compressed) noexcept {
		if (idx >= NumBuffers || (free_mask() & (1u << idx)) == 0u) {
			return false;
		}
		const eng::s32 got = pcm_codec::decode(compressed, m_pcm[idx], m_cfg.compression);
		if (got < 0 || static_cast<eng::u16>(got) != m_cfg.chunk_samples) {
			return false;
		}
		++m_next;
		if (m_next >= m_cfg.num_chunks) {
			m_state.set_eof();
		}
		return m_state.on_chunk_ready(idx);
	}

	/// **IRQ de audio**: avanza el buffer de reproducción. Devuelve `false` si el siguiente no
	/// estaba listo (posible underrun). Al final del stream (`at_end()`) el `false` es el fin
	/// normal, no un underrun real.
	bool advance() noexcept { return m_state.advance(); }

	[[nodiscard]] eng::u8 play_index() const noexcept { return m_state.play_index(); }
	/// Puntero al PCM que suena (para `AUDxLCH/LCL`; `AUDxLEN = chunk_samples/2` en *words*).
	[[nodiscard]] const eng::u8* play_pcm() const noexcept { return m_pcm[m_state.play_index()].data(); }

	/// ¿Se agotaron todos los chunks y no queda ninguno por reproducir? (fin real del stream).
	[[nodiscard]] bool finished() const noexcept { return m_state.finished(); }

	/// ¿El fin del archivo llegó pero todavía suena el último buffer? (fin inminente, sin underrun).
	[[nodiscard]] bool at_end() const noexcept { return m_state.eof() && !m_state.play_ready(); }

	[[nodiscard]] bool eof() const noexcept { return m_state.eof(); }
	[[nodiscard]] bool underrun() const noexcept { return m_state.underrun(); }
	void clear_underrun() noexcept { m_state.clear_underrun(); }

private:
	Config m_cfg {};
	eng::os::ChunkStream<NumBuffers> m_state {};
	eng::Span<eng::u8> m_pcm[NumBuffers] {};
	eng::u16 m_next = 0; ///< próximo chunk a leer/decodificar
};

} // namespace eng::audio
