#pragma once

/// \file stream.hpp
/// **Streaming de chunks** (`eng::os`): máquina de estados de un flujo con **doble/triple buffer**
/// alimentado por E/S asíncrona (la lectura la lanza el backend; aquí está la política de buffers).
/// Paula consume el buffer de reproducción mientras el disco llena el siguiente. Ver
/// `docs/engine/architecture/AUDIO_STREAMING.md` §3 y `MINI_OS_IO.md` §5.
///
/// Es **puro**: no toca memoria ni E/S; el llamador lanza las lecturas de los buffers que
/// `request_mask()` marca y avisa con `on_chunk_ready`.

#include <eng/core/types.hpp>

namespace eng::os {

/// Stream de chunks con `NumBuffers` buffers (2 o 3).
template <eng::u8 NumBuffers>
class ChunkStream {
	static_assert(NumBuffers >= 2u && NumBuffers <= 8u, "ChunkStream: 2..8 buffers");

public:
	static constexpr eng::u8 kNumBuffers = NumBuffers;

	/// Marca el buffer `idx` como **lleno** (llegó su chunk). `false` si el índice es inválido.
	bool on_chunk_ready(eng::u8 idx) noexcept {
		if (idx >= NumBuffers) {
			return false;
		}
		m_ready = static_cast<eng::u8>(m_ready | (1u << idx));
		return true;
	}

	/// ¿Está listo el buffer de reproducción?
	[[nodiscard]] bool play_ready() const noexcept { return (m_ready & (1u << m_play)) != 0u; }

	/// Índice del buffer que suena.
	[[nodiscard]] eng::u8 play_index() const noexcept { return m_play; }

	/// Avanza al siguiente buffer (lo llama el IRQ de audio al agotarse el actual). Devuelve `false`
	/// si el siguiente **no** estaba listo (**underrun**).
	bool advance() noexcept {
		m_ready = static_cast<eng::u8>(m_ready & ~(1u << m_play));
		m_play = static_cast<eng::u8>((m_play + 1u) % NumBuffers);
		if (!play_ready()) {
			m_underrun = true;
			return false;
		}
		return true;
	}

	/// Máscara de buffers **vacíos** (los que hay que mandar leer), o 0 si no hay trabajo o `eof`.
	[[nodiscard]] eng::u8 request_mask() const noexcept {
		if (m_eof) {
			return 0u;
		}
		return static_cast<eng::u8>(static_cast<eng::u8>(~m_ready) & ((1u << NumBuffers) - 1u));
	}

	/// ¿Hay algún buffer libre que mandar leer?
	[[nodiscard]] bool needs_data() const noexcept { return request_mask() != 0u; }

	/// Marca el fin del archivo (no se piden más chunks).
	void set_eof() noexcept { m_eof = true; }
	[[nodiscard]] bool eof() const noexcept { return m_eof; }

	/// ¿Hubo algún underrun (el buffer de reproducción no estaba listo)?
	[[nodiscard]] bool underrun() const noexcept { return m_underrun; }
	void clear_underrun() noexcept { m_underrun = false; }

	/// ¿Todos los buffers consumidos y sin más datos? (fin real del stream).
	[[nodiscard]] bool finished() const noexcept { return m_eof && m_ready == 0u; }

private:
	// `volatile`: el estado lo comparten la IRQ de audio (`advance`/`play_ready`) y el bucle
	// principal (`on_chunk_ready`/`request_mask`); sin `volatile` el compilador cachea el valor
	// (el ISR no es visible para el flujo de datos) y el bucle no ve los buffers liberados.
	volatile eng::u8 m_ready = 0u; ///< bit i = buffer i lleno
	volatile eng::u8 m_play = 0u;  ///< buffer que suena
	volatile bool m_underrun = false;
	volatile bool m_eof = false;
};

} // namespace eng::os
