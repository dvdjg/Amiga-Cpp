#pragma once

/// \file port.hpp
/// **Puerto y colas del mini-SO** (`eng::os`): anillo SPSC IRQ-safe (`MsgQueue`), cola con
/// **prioridad** (`PrioMsgQueue`), puerto (`MsgPort`) y **VBlank latched** (`VBlankLatch`).
/// Ver `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` §5/§10/§13.
///
/// El productor corre en la ISR y el consumidor en el hilo principal: no hay contención porque
/// cada índice lo escribe un solo lado. `signal` es OR-eado (barato desde la ISR); el consumidor
/// drena la cola entera al despertar.

#include <eng/core/ptr.hpp>
#include <eng/core/types.hpp>
#include <eng/os/message.hpp>

namespace eng::os {

/// **Anillo SPSC** de capacidad fija `N` (potencia de dos, indexado con máscara). El productor
/// escribe `head`; el consumidor, `tail`. `push_isr` nunca bloquea: si está lleno, descarta y suma
/// `overflows`.
template <eng::u16 N>
class MsgQueue {
	static_assert((N & (N - 1u)) == 0u, "MsgQueue: N debe ser potencia de dos");

public:
	/// Encola desde la ISR (o con interrupciones deshabilitadas). `false` si está llena: el mensaje
	/// se descarta y `overflows` lo cuenta (nunca bloquea).
	bool push_isr(const Msg& m) noexcept {
		const eng::u16 next = static_cast<eng::u16>((m_head + 1u) & (N - 1u));
		if (next == m_tail) {
			m_overflows = static_cast<eng::u16>(m_overflows + 1u);
			return false;
		}
		m_buf[m_head] = m;
		m_head = next; // publicacion del mensaje
		return true;
	}

	/// Saca el siguiente mensaje (hilo principal). `false` si está vacía.
	bool pop(Msg& out) noexcept {
		if (m_tail == m_head) {
			return false;
		}
		out = m_buf[m_tail];
		m_tail = static_cast<eng::u16>((m_tail + 1u) & (N - 1u));
		return true;
	}

	/// Mira el siguiente mensaje **sin retirarlo**. `false` si está vacía.
	[[nodiscard]] bool peek(Msg& out) const noexcept {
		if (m_tail == m_head) {
			return false;
		}
		out = m_buf[m_tail];
		return true;
	}

	[[nodiscard]] bool empty() const noexcept { return m_head == m_tail; }
	[[nodiscard]] eng::u16 overflows() const noexcept { return m_overflows; }

	/// Profundidad actual (nº de mensajes encolados). Para telemetría (marcas de agua).
	[[nodiscard]] eng::u16 depth() const noexcept {
		return static_cast<eng::u16>((m_head - m_tail) & (N - 1u));
	}

private:
	Msg m_buf[N] {};
	volatile eng::u16 m_head = 0u;      ///< escribe el productor (ISR)
	volatile eng::u16 m_tail = 0u;      ///< escribe el consumidor
	volatile eng::u16 m_overflows = 0u;
};

/// **Prioridad** de un mensaje. Tres niveles bastan en un juego.
enum class MsgPrio : eng::u8 { Low = 0, Normal = 1, High = 2, COUNT };

/// Prioridad por tipo (tabla `constexpr`).
[[nodiscard]] constexpr MsgPrio prio_of(MsgType t) noexcept {
	switch (t) {
	case MsgType::Quit:
	case MsgType::KeyDown:
	case MsgType::KeyUp:
	case MsgType::MouseButton:
		return MsgPrio::High;
	case MsgType::VBlank:
	case MsgType::Timer:
	case MsgType::MouseMove:
	case MsgType::Joystick:
	case MsgType::Gamepad:
		return MsgPrio::Normal;
	default:
		return MsgPrio::Low;
	}
}

/// Señal que despierta el puerto para un tipo de mensaje.
[[nodiscard]] constexpr eng::u32 signal_for(MsgType t) noexcept {
	switch (t) {
	case MsgType::VBlank: return SigVBlank;
	case MsgType::Timer: return SigTimer;
	case MsgType::FileDone:
	case MsgType::FileError:
	case MsgType::DiskChange: return SigFile;
	case MsgType::Quit: return SigQuit | SigHigh;
	case MsgType::KeyDown:
	case MsgType::KeyUp:
	case MsgType::MouseButton: return SigInput | SigHigh;
	case MsgType::MouseMove:
	case MsgType::Joystick:
	case MsgType::Gamepad: return SigInput;
	default: return SigUser;
	}
}

/// **Cola con prioridad**: tres anillos (uno por nivel). `pop` devuelve el de mayor prioridad
/// disponible (los `High` se cuelan), `peek` mira sin retirar y `push_mouse_coalesced` sustituye el
/// último `MouseMove` no consumido (menos spam).
template <eng::u16 N>
class PrioMsgQueue {
	static_assert((N & (N - 1u)) == 0u, "PrioMsgQueue: N debe ser potencia de dos");

public:
	static constexpr eng::u8 kLevels = static_cast<eng::u8>(MsgPrio::COUNT);

	/// Encola en el anillo de su prioridad. `false` si ese anillo está lleno (cuenta `overflows`).
	bool push(const Msg& m, MsgPrio p) noexcept {
		const eng::u8 i = static_cast<eng::u8>(p);
		const eng::u16 next = static_cast<eng::u16>((m_head[i] + 1u) & (N - 1u));
		if (next == m_tail[i]) {
			m_overflows[i] = static_cast<eng::u16>(m_overflows[i] + 1u);
			return false;
		}
		m_buf[i][m_head[i]] = m;
		m_head[i] = next;
		return true;
	}

	/// Retira el de mayor prioridad disponible.
	bool pop(Msg& out, eng::Ref<MsgPrio> out_prio = {}) noexcept {
		for (eng::u8 i = kLevels; i-- > 0u;) {
			if (m_tail[i] == m_head[i]) {
				continue;
			}
			out = m_buf[i][m_tail[i]];
			m_tail[i] = static_cast<eng::u16>((m_tail[i] + 1u) & (N - 1u));
			if (out_prio.valid()) {
				*out_prio = static_cast<MsgPrio>(i);
			}
			return true;
		}
		return false;
	}

	/// Mira el de mayor prioridad sin retirarlo.
	[[nodiscard]] bool peek(Msg& out, eng::Ref<MsgPrio> out_prio = {}) const noexcept {
		for (eng::u8 i = kLevels; i-- > 0u;) {
			if (m_tail[i] == m_head[i]) {
				continue;
			}
			out = m_buf[i][m_tail[i]];
			if (out_prio.valid()) {
				*out_prio = static_cast<MsgPrio>(i);
			}
			return true;
		}
		return false;
	}

	/// ¿Hay algún mensaje de prioridad `>= min`?
	[[nodiscard]] bool has_at_least(MsgPrio min) const noexcept {
		for (eng::u8 i = kLevels; i-- > 0u;) {
			if (i < static_cast<eng::u8>(min)) {
				break;
			}
			if (m_tail[i] != m_head[i]) {
				return true;
			}
		}
		return false;
	}

	/// Coalesce: si el último `MouseMove` de `Normal` no se ha consumido, lo sobrescribe.
	bool push_mouse_coalesced(const Msg& m) noexcept {
		const eng::u8 i = static_cast<eng::u8>(MsgPrio::Normal);
		if (m_head[i] != m_tail[i]) {
			const eng::u16 last = (m_head[i] == 0u) ? static_cast<eng::u16>(N - 1u)
								: static_cast<eng::u16>(m_head[i] - 1u);
			if (m_buf[i][last].type == MsgType::MouseMove) {
				m_buf[i][last] = m;
				return true;
			}
		}
		return push(m, MsgPrio::Normal);
	}

	/// ¿Vacía en todos los niveles?
	[[nodiscard]] bool empty() const noexcept {
		for (eng::u8 i = 0u; i < kLevels; ++i) {
			if (m_tail[i] != m_head[i]) {
				return false;
			}
		}
		return true;
	}

	/// Total de descartes por cola llena (suma de los tres anillos).
	[[nodiscard]] eng::u16 overflows() const noexcept {
		eng::u16 total = 0u;
		for (eng::u8 i = 0u; i < kLevels; ++i) {
			total = static_cast<eng::u16>(total + m_overflows[i]);
		}
		return total;
	}

	/// Profundidad del anillo de prioridad `p`. Para telemetría (marcas de agua).
	[[nodiscard]] eng::u16 depth(MsgPrio p) const noexcept {
		const eng::u8 i = static_cast<eng::u8>(p);
		return static_cast<eng::u16>((m_head[i] - m_tail[i]) & (N - 1u));
	}

	/// Profundidad total (suma de los tres anillos).
	[[nodiscard]] eng::u16 depth_total() const noexcept {
		eng::u16 total = 0u;
		for (eng::u8 i = 0u; i < kLevels; ++i) {
			total = static_cast<eng::u16>(total + ((m_head[i] - m_tail[i]) & (N - 1u)));
		}
		return total;
	}

private:
	Msg m_buf[kLevels][N] {};
	volatile eng::u16 m_head[kLevels] {};
	volatile eng::u16 m_tail[kLevels] {};
	volatile eng::u16 m_overflows[kLevels] {};
};

/// **Puerto de mensajes**: la cola con prioridad más la máscara de señales. `post` es lo que usa el
/// productor (ISR o app); `signal` es lo único que toca la ISR además de encolar.
template <eng::u16 N = 32u>
struct MsgPort {
	PrioMsgQueue<N> queue {};
	volatile eng::u32 signalled = 0u;

	void signal(eng::u32 mask) noexcept { signalled = signalled | mask; }

	/// Bits de señal pedidos que están **puestos, sin consumirlos** (0 si ninguno). No espera:
	/// lo usa `os::wait` para decidir si puede salir (el *bloqueo* lo pone el host).
	[[nodiscard]] eng::u32 pending(eng::u32 mask) const noexcept { return signalled & mask; }

	/// Consume del campo `signalled` los bits pedidos que estén puestos (0 si ninguno). No espera.
	eng::u32 take_signals(eng::u32 mask) noexcept {
		const eng::u32 s = signalled;
		const eng::u32 got = s & mask;
		if (got != 0u) {
			signalled = s & ~mask;
		}
		return got;
	}

	/// Encola aplicando prioridad y coalescing de `MouseMove`, y marca la señal.
	bool post(const Msg& m) noexcept {
		const bool ok = (m.type == MsgType::MouseMove) ? queue.push_mouse_coalesced(m)
							       : queue.push(m, prio_of(m.type));
		if (ok) {
			signal(signal_for(m.type));
		}
		return ok;
	}

	bool pop(Msg& m) noexcept { return queue.pop(m); }
	[[nodiscard]] bool peek(Msg& m) const noexcept { return queue.peek(m); }
	[[nodiscard]] bool empty() const noexcept { return queue.empty(); }
};

/// **VBlank latched**: como máximo un VBlank pendiente, con número de secuencia y contador de los
/// pisados. La ISR llama a `signal`; la app, a `take_vblank`.
struct VBlankLatch {
	volatile eng::u32 sequence = 0u; ///< monotónico; lo incrementa la ISR
	volatile eng::u8 pending = 0u;   ///< 0/1: hay un VBlank sin consumir
	volatile eng::u16 missed = 0u;   ///< cuántos se pisaron sin consumir

	/// La ISR lo llama con la secuencia de frame actual.
	void signal(eng::u32 seq) noexcept {
		if (pending != 0u) {
			missed = static_cast<eng::u16>(missed + 1u);
		}
		sequence = seq;
		pending = 1u;
	}
};

/// Saca el VBlank latched (como máximo uno) y lo deja en `out`. `false` si no hay pendiente.
[[nodiscard]] inline bool take_vblank(VBlankLatch& latch, Msg& out) noexcept {
	if (latch.pending == 0u) {
		return false;
	}
	const eng::u32 seq = latch.sequence;
	const eng::u16 missed = latch.missed;
	latch.pending = 0u;
	latch.missed = 0u;
	out.type = MsgType::VBlank;
	out.time_stamp = seq;
	out.payload.vblank = {seq, missed};
	return true;
}

} // namespace eng::os
