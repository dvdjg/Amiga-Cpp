#pragma once

/// \file clock.hpp
/// **Reloj de mundo** (`eng::rpg`): día + hora + minuto de tiempo de juego, que alimenta la
/// rutina diaria de los NPC, los horarios de tienda, los refugios y los eventos diferidos
/// ("el día 3 a las 18:00"). Es tiempo *de juego* (avanza lo que diga el juego), no el reloj
/// del sistema (`eng::os::time`).
///
/// El minuto es la unidad de la API; el avance usa división nativa del 68000
/// (`divu.w`/mitades de 16 bits, `eng::util::divmod32`), sin libcalls de 32 bits.
///
/// Verificación: HOST-454. NO VERIFICADA por demo (vertical slice en
/// `docs/guides/roadmap/ROADMAP_RPG.md` M6).

#include <eng/core/types/types.hpp>
#include <eng/core/util/intmath.hpp>

namespace eng::rpg {

/// Minutos de un día completo (24 h × 60).
inline constexpr eng::u16 kMinutesPerDay = 1440u;

/// Minutos de una hora.
inline constexpr eng::u16 kMinutesPerHour = 60u;

/// Día del mundo (1 = primer día de la partida; 0xffff se usa como tope saturado).
inline constexpr eng::u16 kLastDay = 0xffffu;

/// Reloj de tiempo de juego. Agregado pequeño: cae bien en un save compuesto y en la pila
/// del 68000 sin aliasing. Invariantes: `hour < 24`, `minute < 60` (los mantienen las
/// funciones de avance; escribir los campos a mano es cosa del juego).
struct WorldClock {
	eng::u16 day = 1u;   ///< día del mundo (1..65535)
	eng::u8 hour = 8u;   ///< hora del día (0..23); la partida arranca a las 08:00
	eng::u8 minute = 0u; ///< minuto de la hora (0..59)
};

/// Minutos transcurridos del día actual (0..1439), para horarios de tienda/rutinas.
[[nodiscard]] inline eng::u16 minutes_of_day(const WorldClock& clock) noexcept {
	return clock.hour * kMinutesPerHour + clock.minute;
}

/// Adelanta el reloj `minutes` minutos, con acarreo de horas y días. `day` satura a
/// `kLastDay` (una partida de 179 años es suficiente); el delta puede ser cualquier `u32`
/// (adelantos largos de "esperar en la posada" sin multiplicar ni dividir en libcall).
inline void advance_minutes(WorldClock& clock, eng::u32 minutes) noexcept {
	eng::u32 rest_of_day = 0u;
	// Total = minutos transcurridos del día + delta; se normaliza a días (divisor 1440) con
	// divu.w nativo: `divmod32` devuelve los días (cociente) y deja en `rest_of_day` el resto.
	const eng::u32 total = minutes_of_day(clock) + minutes;
	const eng::u32 days = eng::util::divmod32(total, kMinutesPerDay, rest_of_day);
	// La hora del día sale de otro divu.w; su resto son los minutos de esa hora.
	eng::u16 minute_of_hour = 0u;
	const eng::u16 hour = eng::util::divu_w(rest_of_day, kMinutesPerHour, minute_of_hour);
	clock.hour = hour;
	clock.minute = minute_of_hour;
	const eng::u32 next_day = clock.day + days;
	clock.day = next_day > kLastDay ? kLastDay : next_day;
}

/// Adelanta `hours` horas (equivale a `advance_minutes(hours * 60)`).
inline void advance_hours(WorldClock& clock, eng::u32 hours) noexcept {
	advance_minutes(clock, hours * 60u);
}

/// Adelanta `days` días conservando la hora del día (dormir no cambia la hora de despertar
/// si el juego no lo dice: solo cuenta días). `day` satura a `kLastDay`.
inline void advance_days(WorldClock& clock, eng::u32 days) noexcept {
	const eng::u32 next_day = clock.day + days;
	clock.day = next_day > kLastDay ? kLastDay : next_day;
}

/// Fija la hora del día recortando al rango válido (24→23:59 sería un bug de contenido).
inline void set_time(WorldClock& clock, eng::u8 hour, eng::u8 minute) noexcept {
	clock.hour = hour < 24u ? hour : 23u;
	clock.minute = minute < 60u ? minute : 59u;
}

} // namespace eng::rpg
