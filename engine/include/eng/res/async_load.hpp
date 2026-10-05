#pragma once

/// \file async_load.hpp
/// **Lectura asíncrona de un fichero** (`eng::res::AsyncRead`, R6.7 de `ROADMAP_RESOURCES.md`):
/// `begin` abre y lanza `os::file_read_async` (el frame **no** se bloquea); el backend Amiga la
/// resuelve como operación **diferida** —el bucle llama `os::file_pump()` y postea `FileDone`/`File`
/// `Error`— y la completa `on_done`. El juego la arranca y la sondea por frame (`state()`), p. ej.
/// una **transición de zona** que prefetchea el código de la siguiente sin parar el juego.
///
/// ```cpp
/// eng::res::AsyncRead load;
/// load.begin("data/code/answer.engz", eng::Span<eng::u8> {buf, sizeof(buf)});
/// // por frame: os::file_pump(); while (os::system_port().pop(m)) load.on_done(m);
/// if (load.done()) { /* buf listo */ }
/// ```
///
/// El `cookie` de la petición es `IoUser{'L', id}` (`tag` de *loader*, distinto del `'A'` de la
/// caché de assets): el `on_done` solo acepta el suyo. La identidad con **generación** para
/// respuestas tardías la aporta `RequestTable` (`eng/os/request.hpp`) al consumidor multiplexado.
///
/// Los `static_cast` a `u32`/`u8` son de **frontera**: los tamaños/resultados de E/S del SO son de
/// ancho **fijo** (`u32`), mientras `eng::usize` es el tamaño de la plataforma (`__SIZE_TYPE__`).

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/os/file.hpp>
#include <eng/os/message.hpp>

namespace eng::res {

/// Estado de una `AsyncRead`.
enum class AsyncReadState : eng::u8 { Idle = 0, Pending, Done, Failed };

/// Ver doc del fichero.
class AsyncRead {
public:
	static constexpr eng::u8 kTag = static_cast<eng::u8>('L'); ///< 'L' loader (≠ 'A' caché)

	/// Abre `path` y lanza la lectura de hasta `dst.size()` bytes (deferida en Amiga). `false`
	/// (estado `Failed`) si no se abre o no se lanza en el acto.
	[[nodiscard]] bool begin(const char* path, eng::Span<eng::u8> dst) noexcept {
		reset();
		if (dst.empty()) {
			m_state = AsyncReadState::Failed;
			return false;
		}
		m_handle = eng::os::file_open(path, eng::os::FileMode::Read);
		if (m_handle == 0u) {
			m_state = AsyncReadState::Failed;
			return false;
		}
		m_bytes = eng::os::file_size(m_handle);
		const eng::u32 n = (m_bytes < static_cast<eng::u32>(dst.size()))
					   ? m_bytes
					   : static_cast<eng::u32>(dst.size());
		if (n == 0u) {
			eng::os::file_close(m_handle);
			m_handle = 0u;
			m_state = AsyncReadState::Failed;
			return false;
		}
		eng::os::IoNotify notify {};
		notify.cookie = eng::os::IoUser {kTag, m_id, 0u}.encode();
		if (!eng::os::file_read_async(m_handle, eng::Span<eng::u8> {dst.data(), n}, 0u, notify)) {
			eng::os::file_close(m_handle);
			m_handle = 0u;
			m_state = AsyncReadState::Failed;
			return false;
		}
		m_requested = n;
		m_state = AsyncReadState::Pending;
		return true;
	}

	/// Completa desde un `FileDone`/`FileError` (acepta solo el cookie propio). `true` si era suya.
	[[nodiscard]] bool on_done(const eng::os::Msg& m) noexcept {
		if (m_state != AsyncReadState::Pending) {
			return false;
		}
		if (m.type != eng::os::MsgType::FileDone && m.type != eng::os::MsgType::FileError) {
			return false;
		}
		const eng::os::IoUser u = eng::os::IoUser::decode(m.payload.file.cookie);
		if (u.tag != kTag || u.id != m_id) {
			return false;
		}
		eng::os::file_close(m_handle);
		m_handle = 0u;
		if (m.type == eng::os::MsgType::FileDone && m.payload.file.result > 0) {
			m_received = static_cast<eng::u32>(m.payload.file.result);
			m_state = AsyncReadState::Done;
		} else {
			m_state = AsyncReadState::Failed;
		}
		return true;
	}

	/// Cancela y libera el handle (el buffer es del llamador; no se toca).
	void reset() noexcept {
		if (m_handle != 0u) {
			eng::os::file_close(m_handle);
			m_handle = 0u;
		}
		m_bytes = 0u;
		m_requested = 0u;
		m_received = 0u;
		m_state = AsyncReadState::Idle;
	}

	[[nodiscard]] AsyncReadState state() const noexcept { return m_state; }
	[[nodiscard]] bool pending() const noexcept { return m_state == AsyncReadState::Pending; }
	[[nodiscard]] bool done() const noexcept { return m_state == AsyncReadState::Done; }
	[[nodiscard]] bool failed() const noexcept { return m_state == AsyncReadState::Failed; }
	[[nodiscard]] eng::u32 file_bytes() const noexcept { return m_bytes; }
	[[nodiscard]] eng::u32 requested() const noexcept { return m_requested; }
	[[nodiscard]] eng::u32 received() const noexcept { return m_received; }

private:
	eng::os::FileHandle m_handle = 0u;
	eng::u16 m_id = 1u; ///< id de E/S de este loader (distinto de los de la caché de assets)
	eng::u32 m_bytes = 0u;     ///< tamaño del fichero
	eng::u32 m_requested = 0u; ///< bytes pedidos
	eng::u32 m_received = 0u;  ///< bytes transferidos
	AsyncReadState m_state = AsyncReadState::Idle;
};

} // namespace eng::res
