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
#include <eng/core/numeric_traits.hpp>
#include <eng/core/scalar_fwd.hpp>
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

/// Suma **saturada** en la representación ancha del acumulador del `dot` fusionado: sin
/// esto, sumar 3-4 productos de 4.12 (`≈2^30` cada uno) desborda `s32` y envuelve en
/// silencio. Hay sobrecarga específica para `s32` (el ancho de `s16*s16`); para `long
/// long` (productos de `s32`) el rango no es alcanzable y se deja la suma normal.
template <typename R>
[[nodiscard]] constexpr R sat_add_repr(R a, R b) {
	return static_cast<R>(a + b);
}
[[nodiscard]] constexpr s32 sat_add_repr(s32 a, s32 b) {
	constexpr s32 mx = limits<s32>::max;
	constexpr s32 mn = limits<s32>::min;
	if (b > 0 && a > static_cast<s32>(mx - b)) return mx;
	if (b < 0 && a < static_cast<s32>(mn - b)) return mn;
	return static_cast<s32>(a + b);
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

/// Producto escalar de dos/tres/cuatro pares con la normalización FUSIONADA: los
/// productos comparten exponente (`Ea+Eb`), se suman **exactos** (con acumulador
/// **saturado**, sin envolver) y se normaliza **una vez**. El resultado se normaliza a
/// `Eb` y se recorta a la representación del SEGUNDO lado (el vector/longitud) con la
/// política del tipo (usar `SaturatePolicy` para que el estrechado final también sature).
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
[[nodiscard]] constexpr Fixed<Rb, Eb, P> dot(Fixed<Ra, Ea, P> a, Fixed<Rb, Eb, P> b,
					     Fixed<Ra, Ea, P> c, Fixed<Rb, Eb, P> d) {
	using WR = typename mul_repr<Ra, Rb>::type;
	using W = Fixed<WR, Ea + Eb, P>;
	const W p0 = a * b;
	const W p1 = c * d;
	const W acc {detail::sat_add_repr(p0.v, p1.v)};
	// El estrechado final satura SIEMPRE (no depende de la política de los operandos):
	// un dot fusionado es una magnitud y envolver en silencio es siempre un error.
	return acc.template retag<SaturatePolicy>()
		.template rescale<Eb>()
		.template cast<Rb>()
		.template retag<P>();
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
	const W acc {detail::sat_add_repr(detail::sat_add_repr(p0.v, p1.v), p2.v)};
	// El estrechado final satura SIEMPRE (no depende de la política de los operandos):
	// un dot fusionado es una magnitud y envolver en silencio es siempre un error.
	return acc.template retag<SaturatePolicy>()
		.template rescale<Eb>()
		.template cast<Rb>()
		.template retag<P>();
}
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
[[nodiscard]] constexpr Fixed<Rb, Eb, P> dot(Fixed<Ra, Ea, P> a, Fixed<Rb, Eb, P> b,
					     Fixed<Ra, Ea, P> c, Fixed<Rb, Eb, P> d,
					     Fixed<Ra, Ea, P> e, Fixed<Rb, Eb, P> f,
					     Fixed<Ra, Ea, P> g, Fixed<Rb, Eb, P> h) {
	using WR = typename mul_repr<Ra, Rb>::type;
	using W = Fixed<WR, Ea + Eb, P>;
	const W p0 = a * b;
	const W p1 = c * d;
	const W p2 = e * f;
	const W p3 = g * h;
	const W acc {detail::sat_add_repr(detail::sat_add_repr(detail::sat_add_repr(p0.v, p1.v), p2.v),
					  p3.v)};
	// El estrechado final satura SIEMPRE (no depende de la política de los operandos):
	// un dot fusionado es una magnitud y envolver en silencio es siempre un error.
	return acc.template retag<SaturatePolicy>()
		.template rescale<Eb>()
		.template cast<Rb>()
		.template retag<P>();
}

/// `a·b + c` con **un solo redondeo** (FMA): el producto es exacto en el exponente
/// doble, `c` se lleva a ese exponente (exacto si `E >= 0`) y se normaliza UNA vez. Un
/// `mac` fusionado vale para series, `dot` y transformaciones.
template <typename R, int E, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> mul_add(Fixed<R, E, P> a, Fixed<R, E, P> b,
					      Fixed<R, E, P> c) {
	using WR = typename mul_repr<R, R>::type;
	using W = Fixed<WR, 2 * E, P>;
	const W p = a * b;
	const W cc = c.template cast<WR>().template rescale<2 * E>();
	const W acc {detail::sat_add_repr(p.v, cc.v)};
	return acc.template retag<SaturatePolicy>()
		.template rescale<E>()
		.template cast<R>()
		.template retag<P>();
}

/// `acc += a·b` con un solo redondeo. Alias de `mul_add`.
template <typename R, int E, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> mac(Fixed<R, E, P> a, Fixed<R, E, P> b,
					   Fixed<R, E, P> acc) {
	return mul_add(a, b, acc);
}

// ============================================================================
//  Operadores de asignacion compuesta e incremento (Fixed como un primitivo)
// ============================================================================

/// Unario `+` (identidad).
template <typename R, int E, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> operator+(Fixed<R, E, P> a) {
	return a;
}

/// Suma/resta en sitio (mismo exponente y representacion).
template <typename R, int E, typename P>
constexpr Fixed<R, E, P>& operator+=(Fixed<R, E, P>& a, Fixed<R, E, P> b) {
	a = a + b;
	return a;
}
template <typename R, int E, typename P>
constexpr Fixed<R, E, P>& operator-=(Fixed<R, E, P>& a, Fixed<R, E, P> b) {
	a = a - b;
	return a;
}
/// `a *= b`: producto normalizado de vuelta al exponente y representacion de `a`.
template <typename R, int E, typename P>
constexpr Fixed<R, E, P>& operator*=(Fixed<R, E, P>& a, Fixed<R, E, P> b) {
	a = dot(a, b);
	return a;
}

/// Pre/post incremento y decremento en una unidad (`one()` = `1.0`).
template <typename R, int E, typename P>
constexpr Fixed<R, E, P>& operator++(Fixed<R, E, P>& a) {
	a = a + scalar_traits<Fixed<R, E, P>>::one();
	return a;
}
template <typename R, int E, typename P>
constexpr Fixed<R, E, P> operator++(Fixed<R, E, P>& a, int) {
	const Fixed<R, E, P> t = a;
	++a;
	return t;
}
template <typename R, int E, typename P>
constexpr Fixed<R, E, P>& operator--(Fixed<R, E, P>& a) {
	a = a - scalar_traits<Fixed<R, E, P>>::one();
	return a;
}
template <typename R, int E, typename P>
constexpr Fixed<R, E, P> operator--(Fixed<R, E, P>& a, int) {
	const Fixed<R, E, P> t = a;
	--a;
	return t;
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
/// `>`/`<=`/`>=` como una sola comparación sobre la representación (sin recalcular).
template <typename R, int E, typename P>
[[nodiscard]] constexpr bool operator>(Fixed<R, E, P> a, Fixed<R, E, P> b) {
	return b.v < a.v;
}
template <typename R, int E, typename P>
[[nodiscard]] constexpr bool operator<=(Fixed<R, E, P> a, Fixed<R, E, P> b) {
	return !(b.v < a.v);
}
template <typename R, int E, typename P>
[[nodiscard]] constexpr bool operator>=(Fixed<R, E, P> a, Fixed<R, E, P> b) {
	return !(a.v < b.v);
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
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
	requires (Ea != Eb)
[[nodiscard]] constexpr bool operator>(Fixed<Ra, Ea, P>, Fixed<Rb, Eb, P>) {
	static_assert(Ea == Eb,
		      "eng::math::Fixed: comparar valores con distinto exponente (p. ej. 4.12 > "
		      "entero) no esta permitido. Convertirlos explicitamente con rescale<Edst>() o "
		      "from_int().");
	return false;
}
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
	requires (Ea != Eb)
[[nodiscard]] constexpr bool operator<=(Fixed<Ra, Ea, P>, Fixed<Rb, Eb, P>) {
	static_assert(Ea == Eb,
		      "eng::math::Fixed: comparar valores con distinto exponente (p. ej. 4.12 <= "
		      "entero) no esta permitido. Convertirlos explicitamente con rescale<Edst>() o "
		      "from_int().");
	return false;
}
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
	requires (Ea != Eb)
[[nodiscard]] constexpr bool operator>=(Fixed<Ra, Ea, P>, Fixed<Rb, Eb, P>) {
	static_assert(Ea == Eb,
		      "eng::math::Fixed: comparar valores con distinto exponente (p. ej. 4.12 >= "
		      "entero) no esta permitido. Convertirlos explicitamente con rescale<Edst>() o "
		      "from_int().");
	return false;
}

// ============================================================================
//  Puntos de extensión para `Fixed` (viven en la cabecera del propio escalar)
// ============================================================================

/// Rasgos numéricos de `Fixed<R,E>`: rango simétrico `[-(2^(bits-1)-1), 2^(bits-1)-1]·2^-E`.
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

/// Rasgos de álgebra de `Fixed<R,E,P>` (producto fusionado con `muls.w`, normalización).
template <typename R, int E, typename P>
struct scalar_traits<Fixed<R, E, P>> {
	using scalar = Fixed<R, E, P>;

	/// Producto INTERNO crudo (sin normalizar): el dot acumula estos y normaliza una
	/// vez, que es lo preciso.
	static constexpr auto inner(scalar a, scalar b) { return a * b; }

	static constexpr scalar zero() { return scalar {0}; }
	static constexpr scalar one() { return scalar {static_cast<R>(static_cast<R>(1) << E)}; }
	static constexpr scalar from_int(int i) {
		if consteval {
			constexpr double mx = numeric_traits<scalar>::max_finite;
			if (!(static_cast<double>(i) >= -mx && static_cast<double>(i) <= mx))
				scalar_from_int_out_of_range();
		}
		return scalar {static_cast<R>(static_cast<R>(i) << E)};
	}
	static constexpr int to_int(scalar a) { return static_cast<int>(a.template rescale<0>().v); }

	/// Normaliza un producto (de cualquier exponente) a este escalar. Un redondeo.
	template <typename Prod>
	static constexpr scalar norm_from(Prod p) {
		return p.template rescale<E>().template cast<R>();
	}

	static constexpr bool needs_normalize = true;
	/// Sumar muchas muestras puede saturar el `s16`; el acumulador ancho es `s32`.
	static constexpr bool wide_accum = true;
};

/// Constante escalar desde un `double` de compilación para `Fixed<R,E>` (cuantiza a `E`
/// bits fraccionarios, redondeo al más cercano).
template <typename R, int E, typename P>
struct scalar_const<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> from(double v) {
		if consteval {
			constexpr double mx = numeric_traits<Fixed<R, E, P>>::max_finite;
			if (!(v >= -mx && v <= mx)) detail::scalar_const_fixed_out_of_range();
		}
		const double scaled = v * static_cast<double>(1 << E);
		const double rounded = scaled < 0.0 ? scaled - 0.5 : scaled + 0.5;
		return Fixed<R, E, P> {static_cast<R>(static_cast<long>(rounded))};
	}
};

/// División explícita para fixed (`s16`): `raw = (a.v << E) / b.v`, saturada. Usa
/// `arith<s16>::div` (en 68000, `divs.w` nativo 32/16) tras comprobar que el cociente cabe
/// en `s16`. Válida para `E <= 15`.
template <int E, typename P>
struct scalar_div<Fixed<s16, E, P>> {
	using S = Fixed<s16, E, P>;
	[[nodiscard]] static constexpr S op(S a, S b) {
		constexpr eng::s32 mx = 32767;
		constexpr eng::s32 mn = -32768;
		if (b.v == 0) return S {static_cast<eng::s16>(a.v < 0 ? mn : mx)};
		const eng::s32 num = static_cast<eng::s32>(a.v) << E;
		const eng::s32 den = b.v;
		const eng::s32 lim = mx * (den < 0 ? -den : den);
		if (num > lim) return S {static_cast<eng::s16>(mx)};
		if (num < -lim) return S {static_cast<eng::s16>(mn)};
		return S {arith<s16>::div(num, static_cast<eng::s16>(den))};
	}
};

/// División de `Fixed<s32,E>`: el intermedio `a.v·2^E` no cabe en 32 bits, así que usa `s64`.
/// **No disponible en m68k** (libcalls de 64 bits); allí usa `Fixed<s16,E>`.
template <int E, typename P>
struct scalar_div<Fixed<s32, E, P>> {
	using S = Fixed<s32, E, P>;
	[[nodiscard]] static constexpr S op(S a, S b) {
#if defined(__m68k__)
		(void)a;
		(void)b;
		static_assert(sizeof(S) == 0u,
			      "eng::math::div_norm de Fixed<s32,E> usaria libgcc de 64 bits en "
			      "m68k; usa Fixed<s16,E> o compila para host/32 bits nativo");
		return S {0};
#else
		constexpr eng::s64 mx = 2147483647LL;
		constexpr eng::s64 mn = -2147483648LL;
		if (b.v == 0) {
			return S {static_cast<eng::s32>(a.v < 0 ? mn : mx)};
		}
		const eng::s64 num = static_cast<eng::s64>(a.v) * (static_cast<eng::s64>(1) << E);
		const eng::s64 q = num / b.v;
		if (q > mx) {
			return S {static_cast<eng::s32>(mx)};
		}
		if (q < mn) {
			return S {static_cast<eng::s32>(mn)};
		}
		return S {static_cast<eng::s32>(q)};
#endif
	}
};

} // namespace eng::math
