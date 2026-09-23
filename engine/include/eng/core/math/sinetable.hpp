#pragma once

/// \file sinetable.hpp
/// **Generador de seno en compile-time**, único en el engine. `SineTable<Amp, Steps>`
/// materializa `Steps` muestras de `Amp * sin(2π·i/Steps)` para `i` en [0, Steps).
///
/// Es elegante y versátil: no hay datos escritos a mano ni tablas copiadas, y cambiar la
/// amplitud o el número de pasos es instanciar otro parámetro de plantilla. Con
/// `SineTable<4096, 4096>` se obtiene la tabla **4.12 exacta** que usan los efectos
/// portados 1:1 (rotaciones, proyecciones, plasma); con `SineTable<64>`, la onda de un
/// scroll. La precisión es la de la **serie de Taylor con reducción al cuadrante** en
/// doble (coincide byte a byte con la tabla del demoscene: verificado por HOST-020).
///
/// Reglas del engine: sin heap, sin RTTI, gnu++23. El `double` vive SOLO en la
/// evaluación constexpr del compilador; la tabla emitida es entera.

#include <eng/core/data/ct_array.hpp>
#include <eng/core/types/types.hpp>

namespace eng {

namespace detail {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = 2.0 * kPi;

/// Seno en doble por reducción a [0, π/2] + serie de Taylor (Horner). `constexpr` no
/// puede llamar `std::sin`, así que se evalúa la serie. `Iter` término se elige en
/// compilación: solo afecta a la **generación** de la tabla (coste de compilación y
/// precisión), no al runtime. Con `Iter = 12` el error (≈1e-16) queda por debajo del
/// truncado de las tablas de 8-12 bits; reducirlo no mejora nada en runtime.
template <int Iter = 12>
constexpr double sin_series(double x) {
	if (x > kPi) {
		x -= kTau; // -> (-pi, pi]
	}
	double sign = 1.0;
	if (x > kPi / 2.0) {
		x = kPi - x; // [pi/2, pi] -> [0, pi/2]
	} else if (x < -kPi / 2.0) {
		x = kPi + x; // (-pi, -pi/2] -> [0, pi/2)
		sign = -1.0;
	} else if (x < 0.0) {
		sign = -1.0;
		x = -x;
	}
	const double x2 = x * x;
	double sum = 0.0;
	double term = x; // x^(2k+1)/(2k+1)!
	for (int k = 0; k < Iter; ++k) {
		sum += term;
		term *= -x2 / static_cast<double>((2 * k + 2) * (2 * k + 3));
	}
	return sign * sum;
}

} // namespace detail

/// Tabla de `Amp * sin(2π·i/Steps)`, truncado hacia cero (como el original).
template <s32 Amp, u32 Steps = 64>
struct SineTable {
	/// Materializada por `ct_array` (el functor sólo se evalúa en compile-time; `k[i]`
	/// con `i` runtime lee la tabla, sin float por frame).
	ct_array<s32, Steps> v {[](usize i) -> s32 { return SineTable::sample(static_cast<u32>(i)); }};

	/// Muestra `i & (Steps-1)` (Steps potencia de dos).
	constexpr s32 operator[](u8 i) const { return v[i & (Steps - 1u)]; }

	/// Valor de la muestra `i` (i < Steps).
	static constexpr s32 sample(u32 i) {
		const double rad = detail::kTau * static_cast<double>(i) / static_cast<double>(Steps);
		return static_cast<s32>(detail::sin_series(rad) * static_cast<double>(Amp));
	}
};

// La tabla Q concreta (4.12, s16, 4096 pasos) es vocabulario retro:
// `eng/retro/sintab.hpp` (`eng::retro::kSinTab`).

} // namespace eng
