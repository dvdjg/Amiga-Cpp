#pragma once

/// \file timer.hpp
/// **Timers de usuario del mini-SO** (`eng::os::TimerService`): timers de software en **frames**
/// (sobre el VBlank) o en **µs** (sobre los ticks del CIA) que postean `MsgType::Timer`. Se
/// pollean una vez por VBlank; no se ejecuta lógica del juego en la ISR. Ver
/// `docs/engine/architecture/MINI_OS_TIME.md` §5 y `TIME-005..008` de
/// `docs/debugging/investigaciones/vblank-timer-inconsistencies.md`.
///
/// Es **puro**: `poll_and_post` recibe `frame_now`/`ticks_now` como parámetros (el backend aporta
/// los valores), así que se valida en host con ticks sintéticos.
///
/// Garantías de robustez (TIME-005..008):
/// - **Comparación wrap-safe**: un timer vence cuando `s32(now - deadline) >= 0`, calculado con
///   `deadline` y `period` acotados a menos de `2^31` (el llamador lo verifica en `start`). Un
///   `now` que cruce `UINT32_MAX` no dispara ni deja de disparar por error.
/// - **Fase preservada**: un periódico avanza `deadline += period` (no `now + period`), así que
///   el periodo no acumula deriva aunque el sondeo llegue tarde.
/// - **Catch-up explícito**: si el sondeo llega varios periodos tarde, la política del timer
///   decide si se condensan los vencimientos (`Coalesce`, cuenta en `expirations`), se salta al
///   siguiente (`SkipToNext`) o se postea uno por cada periodo perdido (`CatchUpAll`). Nunca se
///   oculta el atraso.
/// - **Handle único**: `start` devuelve un `Handle {slot, generation}`; `stop` cancela **una**
///   instancia concreta. Antes, `stop(id)` paraba todas las coincidencias y un id reutilizado
///   podía confundirse con el anterior (TIME-007).

#include <eng/core/types/types.hpp>
#include <eng/os/message.hpp>
#include <eng/os/port.hpp>
#include <eng/os/time.hpp>

namespace eng::os {

/// Unidad de un timer.
enum class TimerUnit : eng::u8 { Frames, Microseconds };

/// **Política de catch-up** de un timer periódico cuando el sondeo llega varios periodos tarde.
enum class TimerCatchUp : eng::u8 {
	Coalesce,   ///< condensa el atraso en un `Timer` con `expirations = periodos` (defecto)
	SkipToNext, ///< descarta el atraso y reprograma desde ahora (no acumula deuda)
	CatchUpAll, ///< un `Timer` por cada periodo vencido (para el que necesite todos los ticks)
};

/// **Identidad** de una instancia de timer: ranura + generación. Dos `start` en la misma ranura a
/// lo largo del tiempo producen handles distintos, así que `stop` y los mensajes tardíos no pueden
/// confundir una instancia nueva con la vieja (TIME-007).
struct TimerHandle {
	static constexpr eng::u16 kInvalid = 0xffffu;
	eng::u8 slot = 0u;
	eng::u16 generation = 0u;

	/// `true` si el handle referencia una instancia real (generación no nula ni centinela).
	[[nodiscard]] constexpr bool valid() const noexcept {
		return generation != 0u && generation != kInvalid;
	}
	/// Igualdad por ranura y generación (dos handles son el mismo timer solo si coinciden ambos).
	[[nodiscard]] constexpr bool operator==(const TimerHandle& o) const noexcept {
		return slot == o.slot && generation == o.generation;
	}
	/// Empaqueta el handle en 16 bits (slot en los 8 altos, generación en los 8 bajos) para que
	/// quepa en el payload del `Msg`. La generación se trunca a 8 bits: suficiente para
	/// distinguir instancias consecutivas de la misma ranura.
	[[nodiscard]] constexpr eng::u16 packed() const noexcept {
		const eng::u16 hi = static_cast<eng::u16>(slot) << 8u;
		return static_cast<eng::u16>(hi | (generation & 0xffu));
	}
};

/// Un slot de timer.
struct TimerSlot {
	bool active = false;
	bool periodic = false;
	TimerUnit unit = TimerUnit::Frames;
	TimerCatchUp catch_up = TimerCatchUp::Coalesce;
	eng::u16 id = 0;      ///< id de usuario (legado; puede repetirse entre ranuras)
	eng::u16 generation = 0u; ///< generación de la instancia (parte del `TimerHandle`)
	eng::u32 deadline = 0; ///< frame o tick de vencimiento (horizonte < 2^31)
	eng::u32 period = 0;   ///< periodo (misma unidad que el timer)
};

inline constexpr eng::u8 kMaxTimers = 16u;
/// Horizonte máximo de un deadline/periodo: se compara con aritmética modular firmada, así que
/// debe quedar por debajo de `2^31` para que `s32(now - deadline)` no se confunda.
inline constexpr eng::u32 kTimerMaxHorizon = 0x7fffffffu;

/// **Servicio de timers** (capacidad fija, sin heap).
class TimerService {
public:
	/// Arranca un timer. `id` 0 = autoasignado (el slot+1); `delay` va en frames (unidad `Frames`)
	/// o en µs (unidad `Microseconds`). Devuelve un `TimerHandle` válido o `{}`/inválido si no hay
	/// slot libre o los parámetros no son razonables.
	///
	/// Un periódico con `delay == 0` se **rechaza** (sería un bucle infinito de mensajes). Un
	/// one-shot con `delay == 0` dispara en el siguiente sondeo (vencimiento inmediato).
	[[nodiscard]] TimerHandle start(eng::u16 id, eng::u32 delay, TimerUnit unit, bool periodic,
					eng::u32 frame_now, eng::u32 ticks_now,
					TimerCatchUp catch_up = TimerCatchUp::Coalesce) noexcept {
		if (periodic && delay == 0u) {
			return TimerHandle {}; // periódico sin periodo: bucle infinito
		}
		const eng::u32 ticks_delay = (unit == TimerUnit::Microseconds) ? us_to_ticks(delay) : 0u;
		const eng::u32 span = (unit == TimerUnit::Frames) ? delay : ticks_delay;
		// El deadline y el periodo deben caber en el horizonte seguro (< 2^31) para la comparación
		// modular. Un periodoµs enorme se rechaza en vez de producir un deadline incoherente.
		if (span >= kTimerMaxHorizon) {
			return TimerHandle {};
		}
		for (eng::u8 i = 0u; i < kMaxTimers; ++i) {
			if (m_slots[i].active) {
				continue;
			}
			TimerSlot& s = m_slots[i];
			s.active = true;
			++m_active;
			s.periodic = periodic;
			s.unit = unit;
			s.catch_up = catch_up;
			s.id = (id != 0u) ? id : static_cast<eng::u16>(i) + 1u;
			++s.generation; // nueva instancia en esta ranura → handle distinto
			if (s.generation == TimerHandle::kInvalid) {
				s.generation = 0u; // envuelve a 0 (nunca se usa kInvalid)
			}
			s.period = span;
			const eng::u32 now = (unit == TimerUnit::Frames) ? frame_now : ticks_now;
			s.deadline = now + span;
			return TimerHandle {i, s.generation};
		}
		return TimerHandle {};
	}

	/// Cancela **una instancia concreta** por su handle. `false` si el slot no está activo o la
	/// generación no coincide (handle obsoleto).
	bool stop(TimerHandle h) noexcept {
		if (!h.valid() || h.slot >= kMaxTimers) {
			return false;
		}
		TimerSlot& s = m_slots[h.slot];
		if (!s.active || s.generation != h.generation) {
			return false;
		}
		s.active = false;
		--m_active;
		return true;
	}

	/// Cancela **todas** las instancias con el `id` de usuario `id`. Devuelve cuántas canceló.
	/// (Se mantiene por compatibilidad con `os::add_timer`; para identidad exacta usar el handle.)
	eng::u8 stop_by_id(eng::u16 id) noexcept {
		eng::u8 n = 0u;
		for (TimerSlot& s : m_slots) {
			if (s.active && s.id == id) {
				s.active = false;
				--m_active;
				++n;
			}
		}
		return n;
	}

	/// Postea `MsgType::Timer` por cada timer vencido y reprograma los periódicos. Devuelve cuántos
	/// mensajes posteó (con `CatchUpAll` puede ser > nº de slots vencidos).
	template <eng::u16 N>
	eng::u16 poll_and_post(MsgPort<N>& port, eng::u32 frame_now,
			       eng::u32 ticks_now) noexcept {
		eng::u16 posted = 0u;
		// Early-out: se llama cada tick (50/s) y sin timers no debe recorrer los 16 slots.
		if (m_active == 0u) {
			return posted;
		}
		for (eng::u8 i = 0u; i < kMaxTimers; ++i) {
			TimerSlot& s = m_slots[i];
			if (!s.active) {
				continue;
			}
			const eng::u32 now = (s.unit == TimerUnit::Frames) ? frame_now : ticks_now;
			// **Wrap-safe**: vencido cuando `now - deadline` es ≥ 0 en aritmética de 32 bits
			// firmada (deadline y period < 2^31 garantizan que no hay ambigüedad).
			if (static_cast<eng::s32>(now - s.deadline) < 0) {
				continue;
			}
			if (!s.periodic) {
				post_one(port, i, s, now, 1u, frame_now);
				++posted;
				s.active = false;
				--m_active;
				continue;
			}
			// Cuántos periodos han vencido desde el deadline (`now - deadline` / period, ≥ 1).
			const eng::u32 late = now - s.deadline;
			eng::u16 due = static_cast<eng::u16>((late / s.period) + 1u);
			if (due == 0u) {
				due = 0xffffu; // acotar si el atraso desborda u16 (no realista en un juego)
			}
			switch (s.catch_up) {
			case TimerCatchUp::CatchUpAll:
				// Un mensaje por cada periodo vencido (deadline original + k*period).
				for (eng::u16 k = 0u; k < due; ++k) {
					const eng::u32 dl = s.deadline + static_cast<eng::u32>(k) * s.period;
					post_one(port, i, s, dl, 1u, frame_now);
					++posted;
				}
				s.deadline += static_cast<eng::u32>(due) * s.period; // fase preservada
				break;
			case TimerCatchUp::SkipToNext:
				// Descarta el atraso y reprograma desde `now` (no acumula deuda).
				s.deadline = now + s.period;
				post_one(port, i, s, now, 1u, frame_now);
				++posted;
				break;
			case TimerCatchUp::Coalesce:
			default:
				// Condensa: un mensaje con `expirations = due`; la fase se preserva avanzando el
				// deadline por bloques de periodo (evita la deriva de `now + period`).
				post_one(port, i, s, s.deadline, due, frame_now);
				++posted;
				s.deadline += static_cast<eng::u32>(due) * s.period;
				break;
			}
		}
		return posted;
	}

	/// Nº de timers activos (O(1), mantenido por `start`/`stop`/`poll_and_post`).
	[[nodiscard]] eng::u8 active_count() const noexcept { return m_active; }

private:
	/// Construye y postea un `MsgType::Timer` con el payload extendido (`id`, `handle`, `deadline`,
	/// `expirations`). `slot` es el índice de la ranura (parte del `TimerHandle`).
	template <eng::u16 N>
	static void post_one(MsgPort<N>& port, eng::u8 slot, const TimerSlot& s, eng::u32 deadline,
			     eng::u16 expirations, eng::u32 frame_now) noexcept {
		Msg m {};
		m.type = MsgType::Timer;
		m.time_stamp = frame_now;
		m.payload.timer.id = s.id;
		m.payload.timer.handle = TimerHandle {slot, s.generation}.packed();
		m.payload.timer.deadline = deadline;
		m.payload.timer.expirations = expirations;
		(void)port.post(m);
	}

	TimerSlot m_slots[kMaxTimers] {};
	eng::u8 m_active = 0u; ///< nº de slots activos (para el early-out de `poll_and_post`)
};

} // namespace eng::os
