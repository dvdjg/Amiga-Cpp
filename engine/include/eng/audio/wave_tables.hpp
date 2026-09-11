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

/// Rellena `Len` muestras con un seno de `freq` Hz muestreado a `rate` Hz,
/// cerrando el bucle EXACTAMENTE en fase (sin clic). La fase en la muestra `i`
/// es `frac(i*freq/rate)`, calculada como posición entera `p = (i*cycles) % Len`
/// con `cycles = round(Len*freq/rate)`; así `p(Len) = 0` y el bucle es continuo.
/// Amplitud con signo (±`amplitude`). `Len` debe ser constante (para que el
/// compilador optimice las divisiones) y >= 2.
///
/// NOTA: no usar `(freq << 22) / rate` como acumulador: para `freq > 1024` el
/// desplazamiento desborda `u32` y el tono sale mal (con clic). Demos 067-075.
template <u32 Len>
inline void synth_tone(u8* dst, u16 freq, u32 rate, s16 amplitude) {
	const u32 cycles = (Len * static_cast<u32>(freq) + rate / 2u) / rate;
	for (u32 i = 0; i < Len; ++i) {
		const u32 p = (i * cycles) % Len;
		const u32 t = p * 64u;
		const u32 idx = (t / Len) & 63u;
		const u32 frac = ((t % Len) * 256u) / Len;
		const s16 a = sine_byte(idx);
		const s16 b = sine_byte(idx + 1u);
		const s16 v = static_cast<s16>(a + (((b - a) * static_cast<s32>(frac)) >> 8));
		dst[i] = static_cast<u8>(static_cast<s8>((static_cast<s32>(v) * amplitude) / 127));
	}
}

/// Genera una secuencia de notas (cada una `NoteLen` muestras, bucle sin clic).
template <u32 NoteLen>
inline void synth_sequence(u8* dst, const u16* freqs, u32 count, u32 rate, s16 amplitude) {
	for (u32 n = 0; n < count; ++n) {
		synth_tone<NoteLen>(dst + n * NoteLen, freqs[n], rate, amplitude);
	}
}

} // namespace eng::audio
