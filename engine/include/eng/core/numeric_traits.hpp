#pragma once

/// \file numeric_traits.hpp
/// **Rasgos numéricos** de un escalar, para que los algoritmos genéricos (interpolación,
/// geometría, series…) sean independientes del tipo y puedan **comprobar en compilación**
/// si un escalar cubre el rango que el algoritmo necesita.
///
/// El problema que resuelve: un mismo algoritmo tiene dominios distintos según el
/// escalar. `MiniFloat16` cubre `[2^-14, 65504]`; un `Fixed<s16,12>` cubre `[-8, 8)`; un
/// entero no representa fracciones. Si se usa el algoritmo fuera de esos límites, el
/// resultado es basura silenciosa (saturación, 0 por underflow, overflow de acumulador).
/// Con los rasgos y `require_range`/`require_division` el error salta **al compilar**, con
/// un mensaje que dice qué falta.
///
/// Todo el metadato es `double` y vive **solo en compilación** (no genera código ni
/// obliga a que el escalar sepa de `double` en runtime). El valor, si hace falta
/// compararlo, pasa por `to_double(S)`.
///
/// Añadir un escalar nuevo: especializar `numeric_traits<S>` con `max_finite`,
/// `min_normal`, `epsilon`, los flags (`is_fractional`, `has_division`, `has_inf`,
/// `has_nan`) y `to_double`. El resto de la librería lo usa tal cual.

#include <eng/core/fixed.hpp>
#include <eng/core/minifloat.hpp>
#include <eng/core/types.hpp>

namespace eng::math {

/// `2^e` exacto en `double` (para los rasgos de los fixed).
constexpr double pow2i(int e) {
	double r = 1.0;
	if (e >= 0) {
		for (int i = 0; i < e; ++i) r *= 2.0;
	} else {
		for (int i = 0; i < -e; ++i) r *= 0.5;
	}
	return r;
}

/// Rasgos numéricos de `S`. Sin definición por defecto: un escalar que quiera usar la
/// librería genérica debe especializarlos (falla con un error claro).
template <typename S>
struct numeric_traits;

template <>
struct numeric_traits<float> {
	static constexpr double max_finite = 3.4028234663852886e38;
	static constexpr double min_normal = 1.1754943508222875e-38;
	static constexpr double epsilon = 1.1920928955078125e-7; ///< ulp relativo en 1.0
	static constexpr bool is_fractional = true;
	static constexpr bool has_division = true;
	static constexpr bool has_inf = true;
	static constexpr bool has_nan = true;
	static constexpr const char* name = "float";
	static constexpr double to_double(float x) { return static_cast<double>(x); }
};

template <>
struct numeric_traits<double> {
	static constexpr double max_finite = 1.7976931348623157e308;
	static constexpr double min_normal = 2.2250738585072014e-308;
	static constexpr double epsilon = 2.2204460492503131e-16;
	static constexpr bool is_fractional = true;
	static constexpr bool has_division = true;
	static constexpr bool has_inf = true;
	static constexpr bool has_nan = true;
	static constexpr const char* name = "double";
	static constexpr double to_double(double x) { return x; }
};

/// Fixed-point `Repr·2^-Exp`: rango simétrico `[-(2^(bits-1)-1), 2^(bits-1)-1]·2^-Exp`.
/// **No** tiene `operator/` (el núcleo lo prohíbe), de ahí `has_division = false`.
template <typename R, int E, typename P>
struct numeric_traits<Fixed<R, E, P>> {
	static constexpr double scale = pow2i(-E);
	static constexpr double max_finite = static_cast<double>(limits<R>::max) * scale;
	static constexpr double min_normal = scale; ///< 1 ulp
	static constexpr double epsilon = scale;    ///< ulp absoluto
	static constexpr bool is_fractional = E > 0;
	static constexpr bool has_division = false;
	static constexpr bool has_inf = false;
	static constexpr bool has_nan = false;
	static constexpr const char* name = "Fixed";
	static constexpr double to_double(Fixed<R, E, P> x) { return static_cast<double>(x.v) * scale; }
};

template <>
struct numeric_traits<MiniFloat16> {
	static constexpr double max_finite = 65504.0;
	static constexpr double min_normal = 6.103515625e-5; ///< 2^-14
	static constexpr double epsilon = 9.765625e-4;       ///< 2^-10 (ulp relativo en 1.0)
	static constexpr bool is_fractional = true;
	static constexpr bool has_division = true;
	static constexpr bool has_inf = true;
	static constexpr bool has_nan = false; ///< overflow → ∞, no hay NaN
	static constexpr const char* name = "MiniFloat16";
	static constexpr double to_double(MiniFloat16 x) {
		return static_cast<double>(static_cast<float>(x));
	}
};

/// Valor del escalar como `double` (solo para comprobaciones de compilación/telemetría).
template <typename S>
constexpr double to_double(S x) {
	return numeric_traits<S>::to_double(x);
}

/// ¿El intervalo `[Lo, Hi]` cabe en el rango representable de `S`?
template <typename S, double Lo, double Hi>
consteval bool range_fits() {
	return numeric_traits<S>::max_finite >= Hi && -numeric_traits<S>::max_finite <= Lo;
}

/// Exige, en compilación, que `S` cubra `[Lo, Hi]`. Un algoritmo con dominio conocido
/// llama aquí y el fallo aparece al instanciarlo, no en runtime.
template <typename S, double Lo, double Hi>
constexpr void require_range() {
	static_assert(range_fits<S, Lo, Hi>(),
		      "eng::math: el rango requerido por el algoritmo no cabe en el escalar "
		      "(revisa numeric_traits<S>)");
}

/// Exige que `S` sepa dividir. Los algoritmos con division (remap, normalize, reflect…)
/// lo comprueban para no caer en un "no matching operator/" opaco.
template <typename S>
constexpr void require_division() {
	static_assert(numeric_traits<S>::has_division,
		      "eng::math: el algoritmo necesita division y el escalar no la ofrece");
}

/// Diagnóstico de compilación: `scalar_traits<Fixed>::from_int` recibe un entero que no
/// cabe en el rango del fixed. Sin esto, `i << E` **envuelve** en silencio (p. ej.
/// `from_int(15)` en 4.12, rango ±8).
void scalar_from_int_out_of_range();

/// ¿El valor (si es constante, en `if consteval`) cae dentro de `[lo, hi]`?
template <typename S>
constexpr bool in_range(S x, double lo, double hi) {
	const double d = to_double(x);
	return d >= lo && d <= hi;
}

} // namespace eng::math
