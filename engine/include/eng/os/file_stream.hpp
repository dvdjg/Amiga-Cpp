#pragma once

/// \file file_stream.hpp
/// **Alimentador de un `ChunkStream` desde un fichero asíncrono** (`eng::os`): por cada buffer
/// **vacío** lanza una lectura **secuencial** (offset siguiente) y, al llegar el `FileDone`, marca
/// el buffer listo. Ver `docs/engine/architecture/AUDIO_STREAMING.md` §3 y `MINI_OS_IO.md` §5.
///
/// La **fuente** de lectura es una **política de plantilla** (`Source`), no un `void*`+función: el
/// tipo se conoce en compilación y `m_source->read_async(...)` es una llamada directa (cero coste,
/// verificada por el compilador). El backend aporta una fuente que envuelve `file_read_async`; el
/// test, una fake síncrona. El `ChunkStream` y la fuente se referencian con `eng::Ref` (observador
/// no propietario y anulable, sin `*` crudo); los buffers, con `eng::Span`.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/os/stream.hpp>

namespace eng::os {

/// Alimenta `ChunkStream<NumBuffers>` desde una fuente `Source` de `total_bytes`, leyendo
/// `chunk_bytes` por buffer. `Source` debe ofrecer:
///   `bool read_async(eng::u8 idx, eng::u32 offset, eng::u8* dst, eng::u32 bytes)`
/// donde `idx` viaja como cookie del `FileDone` (para correlacionar la respuesta).
template <eng::u8 NumBuffers, class Source>
class FileChunkFeeder {
public:
	/// Enlaza el feeder. `buffers` debe tener `NumBuffers * chunk_bytes` (Chip RAM, del llamador).
	void init(eng::Ref<ChunkStream<NumBuffers>> stream, eng::Span<eng::u8> buffers,
		  eng::u32 chunk_bytes, eng::u32 total_bytes, eng::Ref<Source> source) noexcept {
		m_stream = stream;
		m_source = source;
		m_buffers = buffers;
		m_chunk = chunk_bytes;
		m_total = total_bytes;
		m_next = 0u;
		for (eng::u8 i = 0u; i < NumBuffers; ++i) {
			m_in_flight[i] = false;
		}
	}

	/// Lanza las lecturas de los buffers **vacíos y sin lectura en curso** (secuencial). Marca EOF
	/// si se agota el fichero. Llamar al arrancar y tras cada `on_done`.
	void pump() noexcept {
		if (!m_stream.valid() || !m_source.valid() || m_buffers.data() == nullptr) {
			return;
		}
		const eng::u8 mask = m_stream->request_mask();
		for (eng::u8 i = 0u; i < NumBuffers; ++i) {
			if ((mask & (1u << i)) == 0u || m_in_flight[i]) {
				continue;
			}
			if (m_next >= m_total) {
				m_stream->set_eof();
				break;
			}
			eng::u32 bytes = m_chunk;
			if (m_next + bytes > m_total) {
				bytes = m_total - m_next;
			}
			eng::u8* dst = m_buffers.data() + static_cast<eng::u32>(i) * m_chunk;
			if (!m_source->read_async(i, m_next, dst, bytes)) {
				m_stream->set_eof();
				break;
			}
			m_in_flight[i] = true;
			m_next += bytes;
		}
	}

	/// Procesa el `FileDone` del buffer `idx` (su cookie) con `bytes` leídos. `false` si el índice
	/// es inválido o no había lectura en curso.
	bool on_done(eng::u8 idx, eng::u32 bytes) noexcept {
		if (!m_stream.valid() || idx >= NumBuffers || !m_in_flight[idx]) {
			return false;
		}
		m_in_flight[idx] = false;
		if (bytes == 0u) {
			m_stream->set_eof();
			return true;
		}
		if (bytes < m_chunk) {
			m_stream->set_eof(); // lectura corta: este es el último chunk
		}
		return m_stream->on_chunk_ready(idx);
	}

	/// Offset de la próxima lectura (para depuración/tests).
	[[nodiscard]] eng::u32 next_offset() const noexcept { return m_next; }
	/// ¿Hay una lectura en curso para el buffer `idx`?
	[[nodiscard]] bool in_flight(eng::u8 idx) const noexcept {
		return idx < NumBuffers && m_in_flight[idx];
	}

private:
	eng::Ref<ChunkStream<NumBuffers>> m_stream {}; ///< máquina de buffers (no propietario)
	eng::Ref<Source> m_source {};                  ///< fuente de lectura (no propietaria)
	eng::Span<eng::u8> m_buffers {};               ///< `NumBuffers * chunk_bytes` (del llamador)
	eng::u32 m_chunk = 0u;
	eng::u32 m_total = 0u;
	eng::u32 m_next = 0u;
	bool m_in_flight[NumBuffers] {};
};

} // namespace eng::os
