#pragma once

/// \file port.hpp
/// **Puerto de mensajes del mini-SO** (`eng::os`): cola de anillo SPSC (productor IRQ,
/// consumidor en el bucle) y `MsgPort` con `post`/`try_get`. Sustituye la espera activa por un
/// **bucle reactivo** (la app consume mensajes). Sin heap, sin excepciones.
///
/// Diseño canónico: `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` y
/// `engine/include/eng/os/README.md`. El productor es la IRQ (p. ej. BLIT/VERTB del backend);
/// el consumidor, el `GameModule`/`App`.

#include <eng/core/types.hpp>

namespace eng::os {

/// Tipo de mensaje (contiguo).
enum class MsgType : eng::u8 {
	None = 0,
	VBlank,  ///< tick de frame (VERTB)
	BlitDone,///< fin de blit (IRQ BLIT)
	Timer,
	Input,
	User,
};

/// Mensaje: `type` + `code` (subtipo) + `data` (carga útil entera; sin punteros).
struct Msg {
	MsgType type = MsgType::None;
	eng::u16 code = 0;
	eng::u32 data = 0;
};

/// Cola de mensajes de anillo SPSC de capacidad `N` (fijo, sin heap).
///
/// **Un solo productor** (la IRQ) y **un solo consumidor** (el bucle): el productor solo
/// escribe `m_head` y el consumidor solo `m_tail`, así que no hacen falta bloqueos (en un
/// 68000 de un solo núcleo basta con `volatile`). Cola **llena** → `post` devuelve `false`
/// (el productor descarta; política del llamador).
template <eng::u16 N>
class MsgQueue {
	static_assert(N > 0u, "MsgQueue: N debe ser > 0");

public:
	/// Publica `m` (productor). `false` si la cola está llena.
	bool post(const Msg& m) noexcept {
		const eng::u16 next = static_cast<eng::u16>((m_head + 1u) % N);
		if (next == m_tail) {
			return false;
		}
		m_buf[m_head] = m;
		m_head = next;
		return true;
	}

	/// Extrae el siguiente mensaje (consumidor). `false` si vacía.
	bool try_get(Msg& out) noexcept {
		if (m_tail == m_head) {
			return false;
		}
		out = m_buf[m_tail];
		m_tail = static_cast<eng::u16>((m_tail + 1u) % N);
		return true;
	}

	[[nodiscard]] bool empty() const noexcept { return m_head == m_tail; }

	/// Vacía la cola (p. ej. al reiniciar el frame).
	void clear() noexcept { m_tail = m_head; }

private:
	Msg m_buf[N] {};
	volatile eng::u16 m_head = 0;
	volatile eng::u16 m_tail = 0;
};

/// Puerto de mensajes: una cola + una bandera de «señalado» (para `wait`/`signal`). La IRQ
/// hace `post` (y `signal`); el bucle consume con `try_get`.
template <eng::u16 N = 8u>
class MsgPort {
public:
	bool post(const Msg& m) noexcept {
		const bool ok = m_queue.post(m);
		m_signalled = true;
		return ok;
	}

	bool try_get(Msg& out) noexcept { return m_queue.try_get(out); }

	[[nodiscard]] bool empty() const noexcept { return m_queue.empty(); }

	/// `true` si hay mensajes pendientes (equivale a «señalado»).
	[[nodiscard]] bool signalled() const noexcept { return !m_queue.empty(); }

	/// Consume la señal (p. ej. tras drenar el puerto).
	void clear_signal() noexcept { m_signalled = false; }

	/// Vacía la cola y quita la señal (p. ej. al reiniciar el frame).
	void clear() noexcept {
		m_queue.clear();
		m_signalled = false;
	}

private:
	MsgQueue<N> m_queue {};
	volatile bool m_signalled = false;
};

} // namespace eng::os
