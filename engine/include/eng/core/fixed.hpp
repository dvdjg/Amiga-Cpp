#pragma once

/// \file fixed.hpp
/// Escalar fixed-point **genérico** de la librería de matemáticas
/// (`docs/engine/architecture/MATH_LIBRARY.md`). Tres cosas viajan EN EL TIPO:
///
///   1. La **representación** `Repr` (`s16`, `s32`, `float`...).
///   2. El **exponente** `Exp`: bits de fracción (`12` = 4.12, `0` = entero, `24` =
///      8.24). Un valor es `v * 2^-Exp`.
///   3. La **política** (`Policy`): cómo redondear al normalizar y cómo reaccionar al
///      desbordar al estrechar. **No cambia el layout** (mismo `sizeof`, mismo
///      `Repr`), así que el mismo dato empaquetado sirve para cualquier política; lo
///      que cambia es el resultado de las operaciones. Permite elegir por algoritmo el
///      compromiso precisión/rendimiento sin duplicar tipos ni reordenar memoria.
///
/// Reglas de la aritmética (todo en compilación):
///
///   - **Multiplicar** SUMA los exponentes y ENSANCHA la representación (exacto, sin
///     normalizar). `4.12 * 4.12 -> 8.24`; sin el ensanchado, `s16 * s16` desborda.
///   - **Sumar/restar** exige MISMO exponente, representación y política: mezclar 4.12
///     con un entero **no compila**. La mezcla, si hace falta, es explícita
///     (`rescale`, `from_int`, `retag`). El error no es un «no matching function» opaco:
///     hay sobrecargas que sólo existen para disparar un `static_assert` que explica la
///     conversión (comprobado por `tools/check/math-diagnostics.sh`).
///   - **Reescalar** (`rescale<Exp>`) aplica la política de redondeo; acumular en el
///     exponente del producto y reescalar UNA vez es lo más preciso y lo que ya hace
///     lib3d (`normfx(a*b + c*d)`).
///
/// Tres conversiones con nombres que no se pisan: `rescale<Exp>` (exponente),
/// `cast<Repr>` (representación) y `retag<Policy>` (sólo política, sin coste).
///
/// Coste (68000, verificado en el `.s`): `muls.w` nativo, `add.l` para las sumas y un
/// único `asr.l` (o `add` + `asr` con redondeo) por normalización. Sin libcalls.

#include <eng/core/arith.hpp>
#include <eng/core/types.hpp>

namespace eng::math {

// ============================================================================
//  Políticas
// ============================================================================

/// Cómo redondear al bajar de exponente (normalizar). Afecta al resultado, no al
/// layout ni al tamaño.
namespace rounding {
/// Trunca hacia −inf (`asr.l`). Es lo que hace el original (`normfx`): **gratis**.
struct TowardNegInf {
	static constexpr int kind = 0;
};
/// Redondea al más cercano, medio hacia +inf (`add` de `2^(shift-1)` + `asr.l`): un
/// `addi` más por normalización, sin sesgo hacia abajo.
struct HalfUp {
	static constexpr int kind = 1;
};
/// Redondea al par más cercano (banquero): sin sesgo acumulado, el más caro.
struct HalfEven {
	static constexpr int kind = 2;
};
} // namespace rounding

/// Qué hacer si el valor no cabe en la representación destino al estrechar.
namespace overflow {
/// Envuelve (complemento a 2). **Gratis**; es lo que hace un `static_cast` normal.
struct Wrap {
	static constexpr int kind = 0;
};
/// Satura al rango de la representación destino (clamp). Un par de comparaciones.
struct Saturate {
	static constexpr int kind = 1;
};
} // namespace overflow

/// Límites de una representación (sin `<limits>`, para poder cruzar a 68000).
template <typename R>
struct limits;
template <>
struct limits<s16> {
	static constexpr s16 min = -32768;
	static constexpr s16 max = 32767;
};
template <>
struct limits<s32> {
	static constexpr s32 min = -2147483647L - 1;
	static constexpr s32 max = 2147483647L;
};

/// Conjunto de políticas del escalar. Es UN solo parámetro de plantilla y es
/// extensible (p. ej. estrategia de división, o anchura del acumulador) sin tocar los
/// tipos de los usuarios.
struct DefaultPolicy {
	using Round = rounding::TowardNegInf; // fiel al original y gratis
	using Overflow = overflow::Wrap;      // como un cast normal
};

// ============================================================================
//  Promoción de la representación
// ============================================================================

/// El producto de dos `R` necesita más ancho o desborda. Es contrato del algoritmo.
template <typename R>
struct wide;
template <>
struct wide<s16> {
	using type = s32;
};
template <>
struct wide<s32> {
	using type = long long; // no se usa en el camino caliente del 68000
};
template <>
struct wide<float> {
	using type = float; // en coma flotante no hace falta ensanchar
};

/// Representación común de dos (la más ancha): la usa la SUMA, que no puede mezclar
/// anchuras sin perder bits.
template <typename A, typename B>
struct common_repr {
	using type = A; // mismo tipo
};
template <>
struct common_repr<s16, s32> {
	using type = s32;
};
template <>
struct common_repr<s32, s16> {
	using type = s32;
};
template <>
struct common_repr<s16, float> {
	using type = float;
};
template <>
struct common_repr<float, s16> {
	using type = float;
};
template <>
struct common_repr<s32, float> {
	using type = float;
};
template <>
struct common_repr<float, s32> {
	using type = float;
};

/// Representación del PRODUCTO de dos: la común, ensanchada. El exponente del
/// resultado es `Ea + Eb` (lo combina el operador). Toda la coherencia de tipos se
/// resuelve aquí, en compilación: no hay conversiones implícitas ni comprobaciones en
/// runtime.
template <typename A, typename B>
struct mul_repr {
	using type = typename wide<typename common_repr<A, B>::type>::type;
};

namespace detail {

/// `v >> shift` con la política de redondeo (shift > 0).
template <typename R, typename Round>
[[nodiscard]] constexpr R rshift(R v, int shift) {
	if constexpr (Round::kind == rounding::HalfUp::kind) {
		const R half = static_cast<R>(static_cast<R>(1) << (shift - 1));
		return static_cast<R>((v + half) >> shift);
	} else if constexpr (Round::kind == rounding::HalfEven::kind) {
		const R q = static_cast<R>(v >> shift); // truncado hacia -inf
		const R mask = static_cast<R>((static_cast<R>(1) << shift) - 1);
		const R r = static_cast<R>(v & mask); // resto en [0, 2^shift)
		const R half = static_cast<R>(static_cast<R>(1) << (shift - 1));
		if (r > half || (r == half && (q & 1) != 0)) return static_cast<R>(q + 1);
		return q;
	} else {
		return static_cast<R>(v >> shift);
	}
}

} // namespace detail

// ============================================================================
//  El escalar
// ============================================================================

/// Valor fixed-point `v * 2^-Exp` con política `Policy`.
template <typename R, int Exp, typename Policy = DefaultPolicy>
struct Fixed {
	using repr = R;
	using policy = Policy;
	static constexpr int exp = Exp;
	R v;

	/// Mismo valor en otro exponente. Si baja, redondea con `Policy::Round`.
	template <int Edst>
	[[nodiscard]] constexpr Fixed<R, Edst, Policy> rescale() const {
		if constexpr (Edst == Exp) {
			return Fixed<R, Edst, Policy> {v};
		} else if constexpr (Edst < Exp) {
			return Fixed<R, Edst, Policy> {detail::rshift<R, typename Policy::Round>(v, Exp - Edst)};
		} else {
			return Fixed<R, Edst, Policy> {static_cast<R>(v << (Edst - Exp))};
		}
	}

	/// Mismo exponente, otra representación (`cast<Repr>`). Aplica `Policy::Overflow`.
	template <typename R2>
	[[nodiscard]] constexpr Fixed<R2, Exp, Policy> cast() const {
		if constexpr (Policy::Overflow::kind == overflow::Saturate::kind) {
			if (v > limits<R2>::max) return Fixed<R2, Exp, Policy> {limits<R2>::max};
			if (v < limits<R2>::min) return Fixed<R2, Exp, Policy> {limits<R2>::min};
		}
		return Fixed<R2, Exp, Policy> {static_cast<R2>(v)};
	}

	/// Mismo layout, OTRA política: reinterpretación sin coste. Es la forma de usar el
	/// dato empaquetado con el algoritmo que convenga (p. ej. redondeo exacto en el
	/// transform y truncado en una previsualización).
	template <typename P2>
	[[nodiscard]] constexpr Fixed<R, Exp, P2> retag() const {
		return Fixed<R, Exp, P2> {v};
	}
};

/// Política alternativa: redondeo al más cercano (para etapas donde manda precisión).
struct RoundPolicy {
	using Round = rounding::HalfUp;
	using Overflow = overflow::Wrap;
};
/// Política alternativa: saturación al estrechar (nada de envolver).
struct SaturatePolicy {
	using Round = rounding::TowardNegInf;
	using Overflow = overflow::Saturate;
};

// Las convenciones Q (4.12/entero/8.24) NO viven aquí: son vocabulario de la
// especialización retro (`eng/retro/fixed_q.hpp`), no del núcleo genérico.

// ============================================================================
//  Operaciones
// ============================================================================

/// Producto: exponentes SUMAN y la representación se ensancha. Exacto, sin normalizar.
/// Producto: exponentes SUMAN y la representacion se resuelve con mul_repr (la
/// comun, ensanchada). Admite precisiones distintas (p. ej. 4.12 x 2.14).
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
[[nodiscard]] constexpr Fixed<typename mul_repr<Ra, Rb>::type, Ea + Eb, P>
operator*(Fixed<Ra, Ea, P> a, Fixed<Rb, Eb, P> b) {
	using CR = typename common_repr<Ra, Rb>::type;
	using WR = typename mul_repr<Ra, Rb>::type;
	// Por el rasgo rith (y no (s32)a * b): en 68000 da muls.w en vez de __mulsi3.
	return Fixed<WR, Ea + Eb, P> {arith<CR>::mul(static_cast<CR>(a.v), static_cast<CR>(b.v))};
}

/// Suma/resta: MISMO exponente y politica; la representacion se promueve a la comun.
template <typename Ra, typename Rb, int E, typename P>
[[nodiscard]] constexpr Fixed<typename common_repr<Ra, Rb>::type, E, P> operator+(Fixed<Ra, E, P> a,
										  Fixed<Rb, E, P> b) {
	using CR = typename common_repr<Ra, Rb>::type;
	return Fixed<CR, E, P> {static_cast<CR>(static_cast<CR>(a.v) + static_cast<CR>(b.v))};
}
template <typename Ra, typename Rb, int E, typename P>
[[nodiscard]] constexpr Fixed<typename common_repr<Ra, Rb>::type, E, P> operator-(Fixed<Ra, E, P> a,
										  Fixed<Rb, E, P> b) {
	using CR = typename common_repr<Ra, Rb>::type;
	return Fixed<CR, E, P> {static_cast<CR>(static_cast<CR>(a.v) - static_cast<CR>(b.v))};
}
template <typename R, int E, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> operator-(Fixed<R, E, P> a) {
	return Fixed<R, E, P> {static_cast<R>(-a.v)};
}

/// Suma/resta de exponentes distintos: operación INVÁLIDA. En vez del opaco "no
/// matching function" que daría la ausencia de sobrecarga, se declara una que sólo es
/// viable en ese caso y dispara un `static_assert` explicando cómo convertir. Sustituye
/// al silencio del compilador por el diagnóstico (ver `tools/check/math-diagnostics.sh`).
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
	requires (Ea != Eb)
[[nodiscard]] constexpr Fixed<typename common_repr<Ra, Rb>::type, (Ea < Eb ? Ea : Eb), P>
operator+(Fixed<Ra, Ea, P>, Fixed<Rb, Eb, P>) {
	static_assert(Ea == Eb,
		      "eng::math::Fixed: no se pueden sumar/restar valores con distinto exponente "
		      "(p. ej. 4.12 + entero). Convertirlos al mismo exponente de forma explicita "
		      "con rescale<Edst>() (baja/sube la fraccion) o from_int() para un entero.");
	return {};
}
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
	requires (Ea != Eb)
[[nodiscard]] constexpr Fixed<typename common_repr<Ra, Rb>::type, (Ea < Eb ? Ea : Eb), P>
operator-(Fixed<Ra, Ea, P>, Fixed<Rb, Eb, P>) {
	static_assert(Ea == Eb,
		      "eng::math::Fixed: no se pueden sumar/restar valores con distinto exponente "
		      "(p. ej. 4.12 - entero). Convertirlos al mismo exponente de forma explicita "
		      "con rescale<Edst>() (baja/sube la fraccion) o from_int() para un entero.");
	return {};
}

/// Conversión explícita desde un entero (exponente 0). Nunca implícita.
template <typename R>
[[nodiscard]] constexpr Fixed<R, 0> from_int(R i) {
	return Fixed<R, 0> {i};
}

/// Conversión explícita a entero, con la política del tipo.
template <typename R, int E, typename P>
[[nodiscard]] constexpr R to_int(Fixed<R, E, P> a) {
	return static_cast<R>(a.template rescale<0>().v);
}

/// Producto de dos escalares normalizado de vuelta al mismo escalar (un redondeo): la
/// version de un solo termino del `dot` fusionado.
template <typename R, int E, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> dot(Fixed<R, E, P> a, Fixed<R, E, P> b) {
	return (a * b).template rescale<E>().template cast<R>();
}

/// Producto escalar de dos/tres pares con la normalización FUSIONADA: los productos
/// comparten exponente (`Ea+Eb`), se suman **exactos** y se normaliza **una vez**.
/// Producto escalar de dos pares con la normalizacion FUSIONADA y precisiones
/// mixtas: el acumulador es la representacion del producto; el resultado se normaliza
/// a Edst y se recorta a la representacion del SEGUNDO lado (el vector/longitud).
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
[[nodiscard]] constexpr Fixed<Rb, Eb, P> dot(Fixed<Ra, Ea, P> a, Fixed<Rb, Eb, P> b,
					     Fixed<Ra, Ea, P> c, Fixed<Rb, Eb, P> d) {
	using WR = typename mul_repr<Ra, Rb>::type;
	using W = Fixed<WR, Ea + Eb, P>;
	const W p0 = a * b;
	const W p1 = c * d;
	const W acc {static_cast<WR>(p0.v + p1.v)};
	return acc.template rescale<Eb>().template cast<Rb>();
}
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
[[nodiscard]] constexpr Fixed<Rb, Eb, P> dot(Fixed<Ra, Ea, P> a, Fixed<Rb, Eb, P> b,
					     Fixed<Ra, Ea, P> c, Fixed<Rb, Eb, P> d,
					     Fixed<Ra, Ea, P> e, Fixed<Rb, Eb, P> f) {
	using WR = typename mul_repr<Ra, Rb>::type;
	using W = Fixed<WR, Ea + Eb, P>;
	const W p0 = a * b;
	const W p1 = c * d;
	const W p2 = e * f;
	const W acc {static_cast<WR>(p0.v + p1.v + p2.v)};
	return acc.template rescale<Eb>().template cast<Rb>();
}

/// Comparaciones (mismo tipo, exponente y política).
template <typename R, int E, typename P>
[[nodiscard]] constexpr bool operator==(Fixed<R, E, P> a, Fixed<R, E, P> b) {
	return a.v == b.v;
}
template <typename R, int E, typename P>
[[nodiscard]] constexpr bool operator!=(Fixed<R, E, P> a, Fixed<R, E, P> b) {
	return a.v != b.v;
}
template <typename R, int E, typename P>
[[nodiscard]] constexpr bool operator<(Fixed<R, E, P> a, Fixed<R, E, P> b) {
	return a.v < b.v;
}

/// Comparar exponentes distintos también es inválido: la sobrecarga sólo existe para
/// dar el diagnóstico en vez del "no matching function".
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
	requires (Ea != Eb)
[[nodiscard]] constexpr bool operator==(Fixed<Ra, Ea, P>, Fixed<Rb, Eb, P>) {
	static_assert(Ea == Eb,
		      "eng::math::Fixed: comparar valores con distinto exponente (p. ej. 4.12 == "
		      "entero) no esta permitido. Convertirlos explicitamente con rescale<Edst>() o "
		      "from_int().");
	return false;
}
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
	requires (Ea != Eb)
[[nodiscard]] constexpr bool operator!=(Fixed<Ra, Ea, P>, Fixed<Rb, Eb, P>) {
	static_assert(Ea == Eb,
		      "eng::math::Fixed: comparar valores con distinto exponente (p. ej. 4.12 != "
		      "entero) no esta permitido. Convertirlos explicitamente con rescale<Edst>() o "
		      "from_int().");
	return false;
}
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
	requires (Ea != Eb)
[[nodiscard]] constexpr bool operator<(Fixed<Ra, Ea, P>, Fixed<Rb, Eb, P>) {
	static_assert(Ea == Eb,
		      "eng::math::Fixed: comparar valores con distinto exponente (p. ej. 4.12 < "
		      "entero) no esta permitido. Convertirlos explicitamente con rescale<Edst>() o "
		      "from_int().");
	return false;
}

} // namespace eng::math
