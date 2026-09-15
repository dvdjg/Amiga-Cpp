#pragma once

/// \file sintab.hpp
/// Tabla de seno 4.12 EXACTA del original (`libmisc/sintab.c`, 4096 pasos = 2 pi),
/// generada en **constexpr** por el compilador (sin datos embebidos ni script).
///
/// `sin` es trascendente y C++ `constexpr` no puede llamar `std::sin`, asi que aqui se
/// evalua una **serie de Taylor con reduccion al cuadrante** en precision doble. Con 12
/// terminos el error queda por debajo del umbral de truncado en las 4096 muestras: la
/// tabla coincide **byte a byte** con la del demoscene (verificado 4096/4096, 0 diffs;
/// test host HOST-020). Sustituye a la aproximacion de Bhaskara de `sinetable.hpp`,
/// que difiere hasta +-8.
///
/// `math2d::SinTableQ12` y los efectos portados 1:1 (p. ej. `plasma`) la reutilizan.

#include <eng/core/types.hpp>

namespace eng {
namespace detail {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = 2.0 * kPi;

/// Seno en precision doble por reduccion a [0, pi/2] + serie de Taylor (Horner).
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
	for (int k = 0; k < 12; ++k) {
		sum += term;
		term *= -x2 / static_cast<double>((2 * k + 2) * (2 * k + 3));
	}
	return sign * sum;
}

/// Misma expresion double que el generador del original: `i * 2 * pi / 4096`.
constexpr double sin_arg(int i) {
	return static_cast<double>(i) * 2.0 * kPi / 4096.0;
}

} // namespace detail

/// `sin(i * 2 pi / 4096) * 4096` truncado hacia cero (i en [0, 4096)).
struct ExactSinTable {
	s16 v[4096] {};
	constexpr ExactSinTable() {
		for (int i = 0; i < 4096; ++i) {
			v[i] = static_cast<s16>(detail::sin_series(detail::sin_arg(i)) * 4096.0);
		}
	}
};

inline constexpr ExactSinTable kExactSin {};

/// Alias de array para el uso tipico (`kSinTab[i]`).
inline constexpr const s16 (&kSinTab)[4096] = kExactSin.v;

} // namespace eng
