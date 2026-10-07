#pragma once

/// \file wave_tables.hpp
/// **Tablas de forma de onda** para el engine (freestanding, enteras, sin float en
/// runtime). El seno es **genérico sobre el tipo de muestra** `T` (`sine_wave<T>`, con el
/// pico declarado por `wave_traits<T>`): 8 bits para el DMA de Paula, `s16` o el ancho que
/// pida otro consumidor. Las formas de control `triangle_byte`/`square_byte` siguen siendo
/// el vocabulario de 8 bits del engine.
///
/// Cada onda devuelve la muestra `i` de un ciclo de 64 muestras; `i` se envuelve a [0, 64).
/// El cuarto de onda se materializa en **compilación** con la serie de `sinetable.hpp`
/// (mismos valores redondeados que la tabla histórica, byte a byte).
///
/// Uso:
///   eng::audio::synth_tone<2048>(dst_u8, 440u, 44100u, 100);      // Paula (u8)
///   eng::audio::synth_tone<2048, eng::s16>(dst_s16, 440u, 44100u, 30000);

#include <eng/core/math/sinetable.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio {

/// Rasgos del tipo de muestra de audio `T` (punto de extensión): `signed_t` es la onda con
/// signo que se calcula (el byte DMA de Paula, `u8`, se interpreta **con signo**) y `peak`
/// su amplitud de pico. Añadir otro ancho es especializar este trait.
template <class T>
struct wave_traits;

/// Muestra de 8 bits con signo (la onda de librería).
template <>
struct wave_traits<s8> {
	using signed_t = s8;           ///< Onda con signo.
	static constexpr int peak = 127; ///< Pico del formato.
};
/// Byte DMA de Paula: la muestra viaja como `u8` pero su valor es **con signo**.
template <>
struct wave_traits<u8> {
	using signed_t = s8;           ///< Onda con signo (semántica del DMA de Paula).
	static constexpr int peak = 127; ///< Pico del formato.
};
/// Muestra PCM de 16 bits con signo.
template <>
struct wave_traits<s16> {
	using signed_t = s16;            ///< Onda con signo.
	static constexpr int peak = 32767; ///< Pico del formato.
};

namespace detail {

/// Cuarto de onda `round(Peak·sin(2π·i/64))` para `i` en [0, 16], materializado en
/// compilación (sin lambda ni `ct_array`: misma forma que la tabla histórica, byte a byte).
template <class W, int Peak>
struct WaveQuarter {
	W v[17] {}; ///< Muestras del cuarto de onda `i = 0..16`.

	/// Genera el cuarto con la serie de Taylor en doble (solo evaluación de compilación).
	constexpr WaveQuarter() {
		for (int i = 0; i <= 16; ++i) {
			const double rad = eng::detail::kTau * static_cast<double>(i) / 64.0;
			const double x = eng::detail::sin_series(rad) * static_cast<double>(Peak);
			v[i] = static_cast<W>(x + 0.5); // redondeo al más cercano
		}
	}
};

/// Instancia materializada por `(W, Peak)` (constant-initialized, una por compilación).
template <class W, int Peak>
inline constexpr WaveQuarter<W, Peak> kWaveQuarter {};

} // namespace detail

/// Seno de la muestra `i` (ciclo de 64) en el tipo de onda de `T` (su `signed_t`). La
/// onda se pliega por simetría desde el cuarto de onda generado en compilación.
template <class T>
constexpr typename wave_traits<T>::signed_t sine_wave(u32 i) {
	using W = typename wave_traits<T>::signed_t;
	constexpr int kPeak = wave_traits<T>::peak;
	constexpr auto& q = detail::kWaveQuarter<W, kPeak>;
	i &= 63u;
	if (i < 16u) return q.v[i];
	if (i < 32u) return q.v[32u - i];
	// La negación (promoción a `int`) vuelve a `W` por conversión implícita: cabe siempre.
	if (i < 48u) return -q.v[i - 32u];
	return -q.v[64u - i];
}

/// Seno 8-bit con signo (la instancia histórica; == `sine_wave<s8>`), 64 muestras/ciclo.
constexpr s8 sine_byte(u32 i) {
	return sine_wave<s8>(i);
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

/// Rellena `Len` muestras de tipo `T` con un seno de `freq` Hz muestreado a `rate` Hz,
/// cerrando el bucle EXACTAMENTE en fase (sin clic). La fase en la muestra `i`
/// es `frac(i*freq/rate)`, calculada como posición entera `p = (i*cycles) % Len`
/// con `cycles = round(Len*freq/rate)`; así `p(Len) = 0` y el bucle es continuo.
/// Amplitud con signo (±`amplitude`, en unidades del formato). `Len` debe ser constante
/// (para que el compilador optimice las divisiones) y >= 2.
///
/// NOTA: no usar `(freq << 22) / rate` como acumulador: para `freq > 1024` el
/// desplazamiento desborda `u32` y el tono sale mal (con clic). Demos 067-075.
template <u32 Len, class T>
inline void synth_tone(T* dst, u16 freq, u32 rate, s16 amplitude) {
	using W = typename wave_traits<T>::signed_t;
	constexpr int kPeak = wave_traits<T>::peak;
	const u32 cycles = (Len * static_cast<u32>(freq) + rate / 2u) / rate;
	for (u32 i = 0; i < Len; ++i) {
		const u32 p = (i * cycles) % Len;
		const u32 t = p * 64u;
		const u32 idx = (t / Len) & 63u;
		const u32 frac = ((t % Len) * 256u) / Len;
		const s32 a = sine_wave<T>(idx);
		const s32 b = sine_wave<T>(idx + 1u);
		const s32 v = a + (((b - a) * static_cast<s32>(frac)) >> 8);
		// Escala a la amplitud pedida (unidades del formato) y convierte a la muestra.
		dst[i] = static_cast<T>(static_cast<W>((v * amplitude) / kPeak));
	}
}

/// Genera una secuencia de notas (cada una `NoteLen` muestras, bucle sin clic).
template <u32 NoteLen, class T>
inline void synth_sequence(T* dst, const u16* freqs, u32 count, u32 rate, s16 amplitude) {
	for (u32 n = 0; n < count; ++n) {
		synth_tone<NoteLen>(dst + n * NoteLen, freqs[n], rate, amplitude);
	}
}

} // namespace eng::audio
