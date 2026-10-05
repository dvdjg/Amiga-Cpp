#pragma once

/// \file async_overlay.hpp
/// **Carga asíncrona de un overlay `.engz`** (`eng::res::AsyncOverlay`, cierra los `⏳` de R6.4/R6.6):
/// une, **sin bloquear el frame**, la lectura del contenedor (`AsyncRead`), su **decodificación**
/// (`decode_engz`) y la **carga del HUNK** por segmento (`DynLoader`, R6.3). El `FileDone` de la
/// cola del mini-OS es lo que dispara decode+carga: `begin` lanza la E/S y, por frame,
/// `os::file_pump()` + drenar el puerto llamando `on_done(m)` avanza la cadena.
///
/// ```cpp
/// eng::res::AsyncOverlay overlay {dyn, app.memory_manager()};
/// overlay.begin("data/code/zone1.engz", {comp, sizeof comp}, {image, sizeof image});
/// // por frame: os::file_pump(); ... overlay.on_done(m);
/// if (overlay.ready()) { auto fn = (Fn) dyn.symbol(overlay.handle(), "answer"); fn(); overlay.unload(); }
/// ```

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/noncopyable.hpp>
#include <eng/memory/memory_manager.hpp>
#include <eng/os/file.hpp>
#include <eng/os/message.hpp>
#include <eng/res/async_load.hpp>
#include <eng/res/dynloader.hpp>
#include <eng/res/engz.hpp>

namespace eng::res {

/// Ver doc del fichero. `DynLoader` y `MemoryManager` entran por **referencia** (no son observadores
/// `Ref`: la vida del overlay es la de quien lo declara, típicamente el juego/una zona).
class AsyncOverlay : public eng::util::Noncopyable {
public:
	enum class State : eng::u8 { Idle = 0, Reading, Ready, Failed };

	explicit AsyncOverlay(DynLoader& loader) noexcept : m_dl(loader) {}

	/// Arranca la lectura asíncrona del `.engz` en `container`; `scratch` es la salida del decode.
	/// El `MemoryManager` (por `Ref`, no propietario) se fija aquí. `false` (estado `Failed`) si no
	/// se puede abrir/lanzar en el acto.
	[[nodiscard]] bool begin(MemoryManager& mem, const char* path, eng::Span<eng::u8> container,
				 eng::Span<eng::u8> scratch) noexcept {
		unload();
		m_mm = mem;
		m_container = container;
		m_scratch = scratch;
		if (!m_read.begin(path, container)) {
			m_state = State::Failed;
			return false;
		}
		m_state = State::Reading;
		return true;
	}

	/// Completa desde un `FileDone` (llámalo por frame tras `os::file_pump`). Al terminar la
	/// lectura, **decodifica y carga** el HUNK (por segmento). `true` si el mensaje era de esta carga.
	[[nodiscard]] bool on_done(const eng::os::Msg& m) noexcept {
		if (m_state != State::Reading || !m_read.on_done(m)) {
			return false;
		}
		if (m_read.done()) {
			finish_load();
		} else if (m_read.failed()) {
			m_state = State::Failed;
		}
		return true;
	}

	[[nodiscard]] State state() const noexcept { return m_state; }
	[[nodiscard]] bool reading() const noexcept { return m_state == State::Reading; }
	[[nodiscard]] bool ready() const noexcept { return m_state == State::Ready; }
	[[nodiscard]] bool failed() const noexcept { return m_state == State::Failed; }
	[[nodiscard]] LibHandle handle() const noexcept { return m_handle; }
	/// Bytes del `.engz` leídos (para telemetría).
	[[nodiscard]] eng::u32 bytes() const noexcept { return m_read.received(); }

	/// Descarga el overlay y libera la memoria (idempotente); deja el estado en `Idle`.
	void unload() noexcept {
		if (m_handle != 0u && m_mm.valid()) {
			m_dl.unload(m_handle, *m_mm.get());
			m_handle = 0u;
		}
		m_read.reset();
		m_state = State::Idle;
	}

private:
	/// Con los bytes del `.engz` ya en RAM: `decode_engz` y `DynLoader::load` (HUNK por banco).
	void finish_load() noexcept {
		if (!m_mm.valid()) {
			m_state = State::Failed;
			return;
		}
		const auto dec = decode_engz(
			eng::Span<const eng::u8> {m_container.data(), m_read.received()}, m_scratch);
		if (!dec.has_value()) {
			m_state = State::Failed;
			return;
		}
		m_handle = m_dl.declare("overlay");
		if (!m_dl.load(m_handle, eng::Span<eng::u8> {m_scratch.data(), *dec}, *m_mm.get())) {
			m_handle = 0u;
			m_state = State::Failed;
			return;
		}
		m_state = State::Ready;
	}

	DynLoader& m_dl;
	eng::Ref<MemoryManager> m_mm {};
	AsyncRead m_read {};
	eng::Span<eng::u8> m_container {};
	eng::Span<eng::u8> m_scratch {};
	LibHandle m_handle = 0u;
	State m_state = State::Idle;
};

} // namespace eng::res
