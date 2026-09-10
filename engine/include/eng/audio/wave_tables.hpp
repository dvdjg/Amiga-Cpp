#pragma once

/// \file wave_tables.hpp
/// Tablas de forma de onda 8-bit con signo (entero, sin float), para generar
/// muestras de audio en el engine (freestanding, sin libgcc soft-float).
///
/// Cada función devuelve la muestra `i` de un ciclo de 64 muestras; `i` se
/// envuelve a [0, 64). Son `constexpr` y host-testables.

#include <eng/core/types.hpp>

namespace eng::audio {

/// Seno 8-bit con signo (tabla de cuarto de onda), 64 muestras por ciclo.
constexpr s8 sine_byte(u32 i) {
	static constexpr s8 kQuarter[17] = {
		0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127,
	};
	i &= 63u;
	if (i < 16) return kQuarter[i];
	if (i < 32) return kQuarter[32 - i];
	if (i < 48) return static_cast<s8>(-kQuarter[i - 32]);
	return static_cast<s8>(-kQuarter[64 - i]);
}

/// Triangular 8-bit con signo, 64 muestras por ciclo (pico ±127).
constexpr s8 triangle_byte(u32 i) {
	i &= 63u;
	if (i < 32) return static_cast<s8>(static_cast<s16>(i) * 8 - 127);
	return static_cast<s8>(383 - static_cast<s16>(i) * 8);
}

/// Cuadrada 8-bit con signo, 64 muestras por ciclo (mitad alta, mitad baja).
constexpr s8 square_byte(u32 i) {
	return (i & 63u) < 32u ? 127 : -127;
}

} // namespace eng::audio
