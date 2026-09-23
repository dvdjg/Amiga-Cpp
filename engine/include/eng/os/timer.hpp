#pragma once

/// \file timer.hpp
/// **Timers de usuario del mini-SO** (`eng::os::TimerService`): timers de software en **frames**
/// (sobre el VBlank) o en **µs** (sobre los ticks del CIA) que postean `MsgType::Timer` con su id.
/// Se pollean una vez por VBlank; no se ejecuta lógica del juego en la ISR. Ver
/// `docs/engine/architecture/MINI_OS_TIME.md` §5.
///
/// Es **puro**: `poll_and_post` recibe `frame_now`/`ticks_now` como parámetros (el backend aporta
/// los valores), así que se valida en host con ticks sintéticos.

#include <eng/core/types/types.hpp>
#include <eng/os/message.hpp>
#include <eng/os/port.hpp>
#include <eng/os/time.hpp>

namespace eng::os {

/// Unidad de un timer.
enum class TimerUnit : eng::u8 { Frames, Microseconds };

/// Un slot de timer.
struct TimerSlot {
	bool active = false;
	bool periodic = false;
	TimerUnit unit = TimerUnit::Frames;
	eng::u16 id = 0;
	eng::u32 deadline = 0; ///< frame o tick de vencimiento
	eng::u32 period = 0;   ///< periodo (misma unidad que el timer)
};

inline constexpr eng::u8 kMaxTimers = 16u;

/// **Servicio de timers** (capacidad fija, sin heap).
class TimerService {
public:
	/// Arranca un timer. `id` 0 = autoasignado; devuelve el id real o 0 si no hay slot libre.
	/// `delay` va en frames (unidad `Frames`) o en µs (unidad `Microseconds`).
	[[nodiscard]] eng::u16 start(eng::u16 id, eng::u32 delay, TimerUnit unit, bool periodic,
				     eng::u32 frame_now, eng::u32 ticks_now) noexcept {
		for (eng::u8 i = 0u; i < kMaxTimers; ++i) {
			if (m_slots[i].active) {
				continue;
			}
			TimerSlot& s = m_slots[i];
			s.active = true;
			s.periodic = periodic;
			s.unit = unit;
			s.id = (id != 0u) ? id : static_cast<eng::u16>(i + 1u);
			s.period = delay;
			s.deadline = (unit == TimerUnit::Frames) ? frame_now + delay
								 : ticks_now + us_to_ticks(delay);
			return s.id;
		}
		return 0u;
	}

	/// Para un timer por id.
	void stop(eng::u16 id) noexcept {
		for (TimerSlot& s : m_slots) {
			if (s.active && s.id == id) {
				s.active = false;
			}
		}
	}

	/// Postea `MsgType::Timer` por cada timer vencido y reprograma los periódicos. Devuelve cuántos
	/// posteó.
	template <eng::u16 N>
	eng::u16 poll_and_post(MsgPort<N>& port, eng::u32 frame_now,
			       eng::u32 ticks_now) noexcept {
		eng::u16 posted = 0u;
		for (eng::u8 i = 0u; i < kMaxTimers; ++i) {
			TimerSlot& s = m_slots[i];
			if (!s.active) {
				continue;
			}
			const bool fire = (s.unit == TimerUnit::Frames) ? (frame_now >= s.deadline)
									: (ticks_now >= s.deadline);
			if (!fire) {
				continue;
			}
			Msg m {};
			m.type = MsgType::Timer;
			m.time_stamp = frame_now;
			m.payload.timer = {s.id};
			(void)port.post(m);
			++posted;
			if (s.periodic) {
				s.deadline = (s.unit == TimerUnit::Frames)
						     ? frame_now + s.period
						     : ticks_now + us_to_ticks(s.period);
			} else {
				s.active = false;
			}
		}
		return posted;
	}

	/// Nº de timers activos.
	[[nodiscard]] eng::u8 active_count() const noexcept {
		eng::u8 n = 0u;
		for (const TimerSlot& s : m_slots) {
			if (s.active) {
				++n;
			}
		}
		return n;
	}

private:
	TimerSlot m_slots[kMaxTimers] {};
};

} // namespace eng::os
