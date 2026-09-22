#pragma once

/// \file file_stream.hpp
/// **Alimentador de un `ChunkStream` desde un fichero asíncrono** (`eng::os`): por cada buffer
/// **vacío** lanza una lectura **secuencial** (offset siguiente) y, al llegar el `FileDone`, marca
/// el buffer listo. El "leer" se **inyecta** (`ReadFn`), de modo que la política de buffers sea
/// **host-testable** (backend: `file_read_async`; host: un fake síncrono). Ver
/// `docs/engine/architecture/AUDIO_STREAMING.md` §3 y `MINI_OS_IO.md` §5.

#include <eng/core/types.hpp>
#include <eng/os/stream.hpp>

namespace eng::os {

/// Alimenta `ChunkStream<NumBuffers>` desde un fichero de `total_bytes`, leyendo `chunk_bytes` por
/// buffer. El llamador aporta los buffers (Chip RAM) y el `ReadFn` (backend o fake).
template <eng::u8 NumBuffers>
class FileChunkFeeder {
public:
	/// Lectura asíncrona inyectada: lanza `bytes` desde `offset` al buffer `idx` (identificado por
	/// el cookie del `FileDone`); `false` si no se pudo lanzar.
	using ReadFn = bool (*)(void* ctx, eng::u8 idx, eng::u32 offset, eng::u8* dst, eng::u32 bytes);

	/// Enlaza el feeder. `buffers` = `NumBuffers * chunk_bytes` (Chip RAM, del llamador).
	void init(ChunkStream<NumBuffers>* stream, eng::u8* buffers, eng::u32 chunk_bytes,
		  eng::u32 total_bytes, void* ctx, ReadFn read) noexcept {
		m_stream = stream;
		m_buffers = buffers;
		m_chunk = chunk_bytes;
		m_total = total_bytes;
		m_ctx = ctx;
		m_read = read;
		m_next = 0u;
		for (eng::u8 i = 0u; i < NumBuffers; ++i) {
			m_in_flight[i] = false;
		}
	}

	/// Lanza las lecturas de los buffers **vacíos y sin lectura en curso** (secuencial). Marca EOF
	/// si se agota el fichero. Llamar al arrancar y tras cada `on_done`.
	void pump() noexcept {
		if (m_stream == nullptr) {
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
			if (m_read == nullptr ||
			    !m_read(m_ctx, i, m_next, m_buffers + static_cast<eng::u32>(i) * m_chunk, bytes)) {
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
		if (m_stream == nullptr || idx >= NumBuffers || !m_in_flight[idx]) {
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
	ChunkStream<NumBuffers>* m_stream = nullptr;
	eng::u8* m_buffers = nullptr;
	eng::u32 m_chunk = 0u;
	eng::u32 m_total = 0u;
	eng::u32 m_next = 0u;
	void* m_ctx = nullptr;
	ReadFn m_read = nullptr;
	bool m_in_flight[NumBuffers] {};
};

} // namespace eng::os
