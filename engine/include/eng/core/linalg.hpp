#pragma once

/// \file linalg.hpp
/// Álgebra lineal **genérica** sobre un escalar (`docs/engine/architecture/MATH_LIBRARY.md`):
/// `Vec<N,S>`, `Mat<N,S>` (parte lineal) y `Affine<N,SR,SL>` (lineal + traslación).
/// El MISMO código sirve para fixed-point (`eng::math::Fixed`), `float`, `half`…,
/// porque toda la aritmética pasa por `scalar_traits<S>`.
///
/// Reglas de composición (las impone el compilador, no un comentario):
///
///   Mat<N,SR> * Mat<N,SR>   -> Mat<N,SR>      ratio * ratio = ratio
///   Mat<N,SR> * Vec<N,SL>   -> Vec<N,SL>      ratio * longitud = longitud
///   Affine<N,SR,SL> * Vec   -> Vec<N,SL>      M*v + t, con t en LONGITUD
///
/// Los productos de una fila se acumulan **exactos** (en el exponente del producto) y
/// se normalizan **una sola vez** al escalar destino. N es constante, así que los
/// bucles se desenrollan; no se crean matrices temporales.

#include <eng/core/fixed.hpp>
#include <eng/core/types.hpp>

namespace eng::math {

// ============================================================================
//  Rasgos del escalar (lo único que el álgebra lineal necesita saber de S)
// ============================================================================

/// Rasgos de un escalar. La plantilla **primaria** da un comportamiento por defecto
/// válido para cualquier tipo "cuerpo" (float, doble, un complejo, un racional…): usa
/// su `+`, `*` y sus constructores. Un escalar fixed-point define su propia
/// especialización porque el producto cambia de exponente y hay que normalizar.
///
/// Para añadir un escalar nuevo basta con: (1) darle `+`, `-`, `*`, un cero y un uno
/// construibles; (2) especializar `scalar_traits` sólo si necesita algo distinto del
/// comportamiento por defecto (normalización, promoción, etc.).
template <typename S>
struct scalar_traits {
	using scalar = S;
	/// Producto interno de dos componentes (para `dot`). Por defecto, el producto del
	/// cuerpo; un espacio con producto interno distinto (p. ej. complejo con conjugado)
	/// lo redefine aquí.
	static constexpr S inner(S a, S b) { return a * b; }
	static constexpr S zero() { return S {}; }
	static constexpr S one() { return S {1}; }
	/// Los productos ya viven en el mismo espacio: no hay que normalizar.
	template <typename Prod>
	static constexpr S norm_from(Prod p) {
		return static_cast<S>(p);
	}
	static constexpr bool needs_normalize = false;
};

template <typename R, int E, typename P>
struct scalar_traits<Fixed<R, E, P>> {
	using scalar = Fixed<R, E, P>;

	/// Producto INTERNO crudo (sin normalizar): el dot acumula estos y normaliza una
	/// vez, que es lo preciso.
	static constexpr auto inner(scalar a, scalar b) { return a * b; }

	static constexpr scalar zero() { return scalar {0}; }
	static constexpr scalar one() { return scalar {static_cast<R>(static_cast<R>(1) << E)}; }
	static constexpr scalar from_int(int i) { return scalar {static_cast<R>(i) << E}; }
	static constexpr int to_int(scalar a) { return static_cast<int>(a.template rescale<0>().v); }

	/// Normaliza un producto (de cualquier exponente) a este escalar. Un redondeo.
	template <typename Prod>
	static constexpr scalar norm_from(Prod p) {
		return p.template rescale<E>().template cast<R>();
	}

	static constexpr bool needs_normalize = true;
};

template <>
struct scalar_traits<float> {
	using scalar = float;

	static constexpr float inner(float a, float b) { return a * b; }

	static constexpr float zero() { return 0.0f; }
	static constexpr float one() { return 1.0f; }
	static constexpr float from_int(int i) { return static_cast<float>(i); }
	static constexpr int to_int(float a) { return static_cast<int>(a); }

	/// Para `float` el producto ya vive en el mismo espacio: no hay que normalizar.
	template <typename Prod>
	static constexpr float norm_from(Prod p) {
		return static_cast<float>(p);
	}

	static constexpr bool needs_normalize = false;
};

// ============================================================================
//  Vector
// ============================================================================

template <int N, typename S>
struct Vec {
	S v[N];

	static constexpr Vec zero() {
		Vec r {};
		for (int i = 0; i < N; ++i) r.v[i] = scalar_traits<S>::zero();
		return r;
	}
};

template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> operator+(const Vec<N, S>& a, const Vec<N, S>& b) {
	Vec<N, S> r {};
	for (int i = 0; i < N; ++i) r.v[i] = a.v[i] + b.v[i];
	return r;
}
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> operator-(const Vec<N, S>& a, const Vec<N, S>& b) {
	Vec<N, S> r {};
	for (int i = 0; i < N; ++i) r.v[i] = a.v[i] - b.v[i];
	return r;
}

/// Producto escalar: acumula los productos EXACTOS y normaliza UNA vez al escalar.
template <int N, typename S>
[[nodiscard]] constexpr S dot(const Vec<N, S>& a, const Vec<N, S>& b) {
	using T = scalar_traits<S>;
	auto acc = T::inner(a.v[0], b.v[0]); // el producto INTERNO lo define el escalar
	for (int k = 1; k < N; ++k) acc = acc + T::inner(a.v[k], b.v[k]);
	return T::norm_from(acc);
}

// ============================================================================
//  Matriz (parte lineal)
// ============================================================================

template <int N, typename S>
struct Mat {
	S m[N][N];

	static constexpr Mat identity() {
		Mat r {};
		for (int i = 0; i < N; ++i)
			for (int j = 0; j < N; ++j)
				r.m[i][j] = (i == j) ? scalar_traits<S>::one() : scalar_traits<S>::zero();
		return r;
	}
};

template <int N, typename S>
[[nodiscard]] constexpr Mat<N, S> operator+(const Mat<N, S>& a, const Mat<N, S>& b) {
	Mat<N, S> r {};
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j) r.m[i][j] = a.m[i][j] + b.m[i][j];
	return r;
}
template <int N, typename S>
[[nodiscard]] constexpr Mat<N, S> operator-(const Mat<N, S>& a, const Mat<N, S>& b) {
	Mat<N, S> r {};
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j) r.m[i][j] = a.m[i][j] - b.m[i][j];
	return r;
}

/// `r[i][j] = fila_i(a) · columna_j(b)`, con la normalización fusionada por elemento.
template <int N, typename S>
[[nodiscard]] constexpr Mat<N, S> operator*(const Mat<N, S>& a, const Mat<N, S>& b) {
	Mat<N, S> r {};
	for (int i = 0; i < N; ++i) {
		for (int j = 0; j < N; ++j) {
			auto acc = a.m[i][0] * b.m[0][j];
			for (int k = 1; k < N; ++k) acc = acc + a.m[i][k] * b.m[k][j];
			r.m[i][j] = scalar_traits<S>::norm_from(acc);
		}
	}
	return r;
}

/// `ratio * longitud -> longitud`.
template <int N, typename SR, typename SL>
[[nodiscard]] constexpr Vec<N, SL> operator*(const Mat<N, SR>& a, const Vec<N, SL>& v) {
	Vec<N, SL> r {};
	for (int i = 0; i < N; ++i) {
		auto acc = a.m[i][0] * v.v[0];
		for (int k = 1; k < N; ++k) acc = acc + a.m[i][k] * v.v[k];
		r.v[i] = scalar_traits<SL>::norm_from(acc);
	}
	return r;
}

template <int N, typename S>
[[nodiscard]] constexpr Mat<N, S> transpose(const Mat<N, S>& a) {
	Mat<N, S> r {};
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j) r.m[i][j] = a.m[j][i];
	return r;
}

/// Determinante 2x2 (fórmula analítica; sin división): los dos productos comparten
/// exponente, se restan exactos y se normaliza una vez.
template <typename S>
[[nodiscard]] constexpr S determinant(const Mat<2, S>& a) {
	const auto d = (a.m[0][0] * a.m[1][1]) - (a.m[0][1] * a.m[1][0]);
	return scalar_traits<S>::norm_from(d);
}

// Nota: el determinante 3x3 necesita productos ENCADENADOS (`a*b*c`), que multiplican
// exponentes otra vez y mezclan representaciones. Queda pendiente de una policy de
// anchura de acumulador (ver el roadmap), no de un `*` ingenuo.

// ============================================================================
//  Transformación afín (lineal + traslación)
// ============================================================================

/// Transformación afín: parte LINEAL (`SR`, un RATIO) y traslación (`SL`, una
/// LONGITUD). Es la forma que hace `M*v + t` con las unidades correctas.
template <int N, typename SR, typename SL>
struct Affine {
	Mat<N, SR> m;
	Vec<N, SL> t;

	static constexpr Affine identity() { return Affine {Mat<N, SR>::identity(), Vec<N, SL>::zero()}; }
};

/// `M*p + t`, con los productos normalizados a LONGITUD y la traslación sumada aparte.
template <int N, typename SR, typename SL>
[[nodiscard]] constexpr Vec<N, SL> transform(const Affine<N, SR, SL>& a, const Vec<N, SL>& p) {
	Vec<N, SL> r {};
	for (int i = 0; i < N; ++i) {
		auto acc = a.m.m[i][0] * p.v[0];
		for (int k = 1; k < N; ++k) acc = acc + a.m.m[i][k] * p.v[k];
		r.v[i] = scalar_traits<SL>::norm_from(acc) + a.t.v[i];
	}
	return r;
}

/// Composición `a ∘ b` (aplica b y luego a): `m = a.m*b.m`, `t = a.m*b.t + a.t`.
template <int N, typename SR, typename SL>
[[nodiscard]] constexpr Affine<N, SR, SL> compose(const Affine<N, SR, SL>& a, const Affine<N, SR, SL>& b) {
	return Affine<N, SR, SL> {a.m * b.m, transform(a, b.t)};
}

/// Sumar una traslación (LONGITUD) a la columna de traslación. Tipada: no se suma un
/// RATIO a una LONGITUD.
template <int N, typename SR, typename SL>
constexpr void translate(Affine<N, SR, SL>& a, const Vec<N, SL>& d) {
	for (int i = 0; i < N; ++i) a.t.v[i] = a.t.v[i] + d.v[i];
}

} // namespace eng::math
