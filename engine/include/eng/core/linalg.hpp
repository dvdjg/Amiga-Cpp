#pragma once
#include <eng/core/scalar_fwd.hpp>

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
#include <eng/core/minifloat.hpp>
#include <eng/core/numeric_traits.hpp>
#include <eng/core/types.hpp>

namespace eng::math {

namespace detail {

/// Mismo escalar exacto (tipo y exponente): la suma/resta de vectores y matrices lo exige.
template <typename A, typename B>
inline constexpr bool same_scalar = false;
template <typename A>
inline constexpr bool same_scalar<A, A> = true;

/// ¿Se pueden multiplicar los dos escalares? Si no, un `Mat*Vec`/`Mat*Mat` mezclado
/// (p. ej. `Mat<3,float> * Vec<3,q12>`) no tiene sentido de dominio.
template <typename A, typename B>
concept mulable = requires(A a, B b) { a * b; };

} // namespace detail

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
	/// Conversión desde/hacia entero crudo (por defecto, el `static_cast` del escalar).
	static constexpr S from_int(int i) { return static_cast<S>(i); }
	static constexpr int to_int(S a) { return static_cast<int>(a); }
	/// ¿Conviene acumular en un ancho mayor (p. ej. `Fixed<s16>` → s32) al sumar muchas
	/// muestras? Lo consulta `stats` para no saturar. Por defecto, acumula en `S`.
	static constexpr bool wide_accum = false;
	/// Los productos ya viven en el mismo espacio: no hay que normalizar.
	template <typename Prod>
	static constexpr S norm_from(Prod p) {
		return static_cast<S>(p);
	}
	static constexpr bool needs_normalize = false;
};

template <>
struct scalar_traits<float> {
	using scalar = float;
	static constexpr bool wide_accum = false;

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

/// Coma flotante de 64 bits: mismos rasgos que `float` (el álgebra es agnóstica del
/// ancho). Necesario para usar `Vec`/`Mat`/`interp`/`geometry` con `double` en host.
template <>
struct scalar_traits<double> {
	using scalar = double;
	static constexpr bool wide_accum = false;

	static constexpr double inner(double a, double b) { return a * b; }

	static constexpr double zero() { return 0.0; }
	static constexpr double one() { return 1.0; }
	static constexpr double from_int(int i) { return static_cast<double>(i); }
	static constexpr int to_int(double a) { return static_cast<int>(a); }

	template <typename Prod>
	static constexpr double norm_from(Prod p) {
		return static_cast<double>(p);
	}

	static constexpr bool needs_normalize = false;
};

namespace detail {

/// ¿El escalar `A` ofrece un multiply-accumulate de un solo redondeo para los operandos
/// `(A, B)` y acumulador `Acc`? (`MiniFloat16` sí; `Fixed` no lo expone a este nivel).
template <typename A, typename B, typename Acc>
concept has_mac = requires(A a, B b, Acc acc) { scalar_traits<A>::mac(a, b, acc); };

/// Acumula `a·b` en `acc` con el mejor redondeo disponible: `mac` del escalar si lo
/// tiene (p. ej. FMA de `MiniFloat16`, 1 redondeo), si no `acc + a*b` (que en fixed ya
/// es exacto en el exponente ancho).
template <typename Acc, typename A, typename B>
[[nodiscard]] constexpr Acc mac_acc(Acc acc, A a, B b) {
	if constexpr (has_mac<A, B, Acc>) {
		return scalar_traits<A>::mac(a, b, acc);
	} else {
		return static_cast<Acc>(acc + a * b);
	}
}

} // namespace detail

/// Producto de dos escalares **normalizado al propio escalar**: identidad para
/// `float`/`MiniFloat16` (su producto ya vive en el mismo espacio) y `rescale` para un
/// fixed (donde `a*b` pasa a exponente doble). Es lo que hace genéricos `lerp`,
/// `cross2`, `rotate2`, `vscale`… sobre CUALQUIER escalar, incluido el fixed sin
/// `operator/`.
template <typename S>
[[nodiscard]] constexpr S mul_norm(S a, S b) {
	return scalar_traits<S>::norm_from(a * b);
}

/// División del escalar. El núcleo de `Fixed` **no** define `operator/` a propósito; un
/// algoritmo que necesite dividir un fixed pide por aquí y la plataforma/tipo aporta la
/// política explícita (para el fixed retro es una división saturante con `divs.w`).
template <typename S>
struct scalar_div {
	static constexpr S op(S a, S b) { return a / b; }
};

/// Cociente `a / b` como el propio escalar (a través de `scalar_div<S>`).
template <typename S>
[[nodiscard]] constexpr S div_norm(S a, S b) {
	return scalar_div<S>::op(a, b);
}

// ============================================================================
//  Vector
// ============================================================================

template <int N, typename S>
struct Vec {
	S v[N];

	/// Acceso por nombre a las componentes (sólo si `N` las tiene): evita el índice
	/// mágico en el código 2D/3D. Devuelve la referencia, así sirve para leer y escribir.
	constexpr S& x() {
		static_assert(N >= 2, "eng::math::Vec::x() requiere N >= 2");
		return v[0];
	}
	constexpr const S& x() const {
		static_assert(N >= 2, "eng::math::Vec::x() requiere N >= 2");
		return v[0];
	}
	constexpr S& y() {
		static_assert(N >= 2, "eng::math::Vec::y() requiere N >= 2");
		return v[1];
	}
	constexpr const S& y() const {
		static_assert(N >= 2, "eng::math::Vec::y() requiere N >= 2");
		return v[1];
	}
	constexpr S& z() {
		static_assert(N >= 3, "eng::math::Vec::z() requiere N >= 3");
		return v[2];
	}
	constexpr const S& z() const {
		static_assert(N >= 3, "eng::math::Vec::z() requiere N >= 3");
		return v[2];
	}

	/// Acceso por índice (como un array); alternativa a `.v[i]`.
	constexpr S& operator[](int i) { return v[i]; }
	constexpr const S& operator[](int i) const { return v[i]; }

	static constexpr Vec zero() {
		Vec r {};
		for (int i = 0; i < N; ++i) r.v[i] = scalar_traits<S>::zero();
		return r;
	}
};

/// Operadores de `Vec`: **suma** y **resta** componente a componente (mismo escalar). El
/// diagnóstico de tipos/dimensiones mixtas tiene sus propias sobrecargas más abajo. Los usan
/// `geometry`/`interp` y las demos.
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

/// Negación de `Vec`: cambia el signo de cada componente.
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> operator-(const Vec<N, S>& a) {
	Vec<N, S> r {};
	for (int i = 0; i < N; ++i) r.v[i] = -a.v[i];
	return r;
}

/// Suma/resta de vectores de distinta dimensión o escalar: operación INVÁLIDA. La
/// sobrecarga sólo existe para dar el diagnóstico (en vez del "no matching function").
template <int N, typename S, int M, typename T>
	requires (N != M || !detail::same_scalar<S, T>)
[[nodiscard]] constexpr Vec<N, S> operator+(const Vec<N, S>&, const Vec<M, T>&) {
	static_assert(N == M, "eng::math: sumar Vec de distinta dimension (N != M).");
	static_assert(detail::same_scalar<S, T>,
		      "eng::math: sumar Vec con escalares distintos (p. ej. float + 4.12). Usa el "
		      "MISMO escalar: convierte con rescale<Exp>()/cast<Repr>()/from_int().");
	return {};
}
template <int N, typename S, int M, typename T>
	requires (N != M || !detail::same_scalar<S, T>)
[[nodiscard]] constexpr Vec<N, S> operator-(const Vec<N, S>&, const Vec<M, T>&) {
	static_assert(N == M, "eng::math: restar Vec de distinta dimension (N != M).");
	static_assert(detail::same_scalar<S, T>,
		      "eng::math: restar Vec con escalares distintos (p. ej. float - 4.12). Usa el "
		      "MISMO escalar: convierte con rescale<Exp>()/cast<Repr>()/from_int().");
	return {};
}

/// Unario `+` (identidad): completa el conjunto de operadores del álgebra.
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> operator+(const Vec<N, S>& a) {
	return a;
}
template <int N, typename S>
constexpr Vec<N, S>& operator+=(Vec<N, S>& a, const Vec<N, S>& b) {
	for (int i = 0; i < N; ++i) a.v[i] = a.v[i] + b.v[i];
	return a;
}
template <int N, typename S>
constexpr Vec<N, S>& operator-=(Vec<N, S>& a, const Vec<N, S>& b) {
	for (int i = 0; i < N; ++i) a.v[i] = a.v[i] - b.v[i];
	return a;
}

/// Producto por un escalar (normalizado: vale también para fixed); conmutativo.
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> operator*(const Vec<N, S>& a, S k) {
	Vec<N, S> r {};
	for (int i = 0; i < N; ++i) r.v[i] = mul_norm(a.v[i], k);
	return r;
}
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> operator*(S k, const Vec<N, S>& a) {
	return a * k;
}
/// División por un escalar (a través de `scalar_div`: saturante en fixed).
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> operator/(const Vec<N, S>& a, S k) {
	Vec<N, S> r {};
	for (int i = 0; i < N; ++i) r.v[i] = scalar_div<S>::op(a.v[i], k);
	return r;
}
template <int N, typename S>
constexpr Vec<N, S>& operator*=(Vec<N, S>& a, S k) {
	a = a * k;
	return a;
}
template <int N, typename S>
constexpr Vec<N, S>& operator/=(Vec<N, S>& a, S k) {
	a = a / k;
	return a;
}

template <int N, typename S>
[[nodiscard]] constexpr bool operator==(const Vec<N, S>& a, const Vec<N, S>& b) {
	for (int i = 0; i < N; ++i) {
		if (!(a.v[i] == b.v[i])) {
			return false;
		}
	}
	return true;
}
template <int N, typename S>
[[nodiscard]] constexpr bool operator!=(const Vec<N, S>& a, const Vec<N, S>& b) {
	return !(a == b);
}

/// Producto escalar: acumula los productos EXACTOS y normaliza UNA vez al escalar.
template <int N, typename S>
[[nodiscard]] constexpr S dot(const Vec<N, S>& a, const Vec<N, S>& b) {
	using T = scalar_traits<S>;
	auto acc = T::inner(a.v[0], b.v[0]); // el producto INTERNO lo define el escalar
	for (int k = 1; k < N; ++k) acc = acc + T::inner(a.v[k], b.v[k]);
	return T::norm_from(acc);
}

/// `dot` de vectores de distinta dimensión o escalar: diagnóstico en vez de "no match".
template <int N, typename S, int M, typename T>
	requires (N != M || !detail::same_scalar<S, T>)
[[nodiscard]] constexpr S dot(const Vec<N, S>&, const Vec<M, T>&) {
	static_assert(N == M, "eng::math: dot de Vec de distinta dimension (N != M).");
	static_assert(detail::same_scalar<S, T>,
		      "eng::math: dot de Vec con escalares distintos (p. ej. float y 4.12).");
	return {};
}

/// `fila · vector`: es el `dot` de N pares con la normalización FUSIONADA (los productos
/// comparten exponente, se suman exactos y se normaliza una vez al escalar del vector).
/// Lee una fórmula de transformación como lo que es: la fila `i` de `M*v`.
template <int N, typename SR, typename SL>
[[nodiscard]] constexpr SL dot(const SR* row, const Vec<N, SL>& v) {
	using WR = typename mul_repr<typename SR::repr, typename SL::repr>::type;
	using W = Fixed<WR, SR::exp + SL::exp, typename SR::policy>;
	W acc = row[0] * v.v[0];
	for (int k = 1; k < N; ++k) {
		acc = acc + row[k] * v.v[k];
	}
	return acc.template rescale<SL::exp>().template cast<typename SL::repr>();
}

// ============================================================================
//  Rectángulo (AABB 2D) — genérico sobre el escalar
// ============================================================================

/// Rectángulo alineado a ejes en 2D, en semántica **min/max** (bordes), no origen+tamaño.
/// Es genérico: sirve para coordenadas enteras (ventanas de recorte) o cualquier escalar.
/// Un rect de origen+tamaño (p. ej. el viewport/canvas de un `Surface`) es otra
/// abstracción y no se representa con este tipo.
template <typename S>
struct Rect {
	S minX {};
	S minY {};
	S maxX {};
	S maxY {};
};

// ============================================================================
//  Matriz (parte lineal)
// ============================================================================

template <int N, typename S>
struct Mat {
	S m[N][N];

	/// Fila `i` (contigua en memoria): para `dot(fila, vector)` y bucles de fórmula.
	[[nodiscard]] constexpr const S* row(int i) const { return m[i]; }
	[[nodiscard]] constexpr S* row(int i) { return m[i]; }

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
			for (int k = 1; k < N; ++k) acc = detail::mac_acc(acc, a.m[i][k], b.m[k][j]);
			r.m[i][j] = scalar_traits<S>::norm_from(acc);
		}
	}
	return r;
}

/// Unario `+`, asignación compuesta y comparación (completan el álgebra de la matriz).
template <int N, typename S>
[[nodiscard]] constexpr Mat<N, S> operator+(const Mat<N, S>& a) {
	return a;
}
template <int N, typename S>
constexpr Mat<N, S>& operator+=(Mat<N, S>& a, const Mat<N, S>& b) {
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j) a.m[i][j] = a.m[i][j] + b.m[i][j];
	return a;
}
template <int N, typename S>
constexpr Mat<N, S>& operator-=(Mat<N, S>& a, const Mat<N, S>& b) {
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j) a.m[i][j] = a.m[i][j] - b.m[i][j];
	return a;
}
/// `a *= b`: producto matricial (el resultado vive en el mismo escalar).
template <int N, typename S>
constexpr Mat<N, S>& operator*=(Mat<N, S>& a, const Mat<N, S>& b) {
	a = a * b;
	return a;
}
/// Producto por un escalar (componente a componente; normalizado para fixed).
template <int N, typename S>
[[nodiscard]] constexpr Mat<N, S> operator*(const Mat<N, S>& a, S k) {
	Mat<N, S> r {};
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j) r.m[i][j] = mul_norm(a.m[i][j], k);
	return r;
}
template <int N, typename S>
[[nodiscard]] constexpr Mat<N, S> operator*(S k, const Mat<N, S>& a) {
	return a * k;
}
template <int N, typename S>
[[nodiscard]] constexpr bool operator==(const Mat<N, S>& a, const Mat<N, S>& b) {
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < N; ++j)
			if (!(a.m[i][j] == b.m[i][j])) {
				return false;
			}
	return true;
}
template <int N, typename S>
[[nodiscard]] constexpr bool operator!=(const Mat<N, S>& a, const Mat<N, S>& b) {
	return !(a == b);
}

/// `Mat*Mat` con escalares distintos: diagnóstico en vez de "no match".
template <int N, typename S, typename T>
	requires (!detail::same_scalar<S, T>)
[[nodiscard]] constexpr Mat<N, S> operator*(const Mat<N, S>&, const Mat<N, T>&) {
	static_assert(detail::same_scalar<S, T>,
		      "eng::math: Mat*Mat con escalares distintos (p. ej. float y 4.12). Usa el "
		      "MISMO escalar en los dos factores.");
	return {};
}

/// `ratio * longitud -> longitud`.
template <int N, typename SR, typename SL>
	requires detail::mulable<SR, SL>
[[nodiscard]] constexpr Vec<N, SL> operator*(const Mat<N, SR>& a, const Vec<N, SL>& v) {
	Vec<N, SL> r {};
	for (int i = 0; i < N; ++i) {
		auto acc = a.m[i][0] * v.v[0];
		for (int k = 1; k < N; ++k) acc = detail::mac_acc(acc, a.m[i][k], v.v[k]);
		r.v[i] = scalar_traits<SL>::norm_from(acc);
	}
	return r;
}

/// `Mat*Vec` con escalares que no se pueden multiplicar (p. ej. `Mat<3,float>` por
/// `Vec<3,q12>`): el dominio lo prohíbe; aquí se explica en vez de fallar dentro del bucle.
template <int N, typename SR, typename SL>
	requires (!detail::mulable<SR, SL>)
[[nodiscard]] constexpr Vec<N, SL> operator*(const Mat<N, SR>&, const Vec<N, SL>&) {
	static_assert(detail::mulable<SR, SL>,
		      "eng::math: Mat*Vec con escalares incompatibles (p. ej. Mat<3,float> * "
		      "Vec<3,q12>). Usa el MISMO escalar en la matriz y el vector.");
	return {};
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

/// Inversa 2x2 analítica (`adj / det`, con `div_norm`/`mul_norm`): funciona sobre cualquier
/// escalar con `scalar_div` (incluido `Fixed`, sin `operator/`). `det = 0` → resultado saturado.
template <typename S>
[[nodiscard]] constexpr Mat<2, S> inverse(const Mat<2, S>& a) {
	const S inv = div_norm(scalar_traits<S>::one(), determinant(a));
	Mat<2, S> r {};
	r.m[0][0] = mul_norm(a.m[1][1], inv);
	r.m[0][1] = -mul_norm(a.m[0][1], inv);
	r.m[1][0] = -mul_norm(a.m[1][0], inv);
	r.m[1][1] = mul_norm(a.m[0][0], inv);
	return r;
}

/// Determinante 3x3 por **cofactores 2x2** (regla de Laplace): cada menor se normaliza a `S`
/// y la suma final de los tres productos se normaliza una vez. Evita los productos
/// encadenados `a*b*c` (que pedirían 64 bits en `Fixed`); para `Fixed` tiene la misma
/// semántica de saturación que `dot` (válido mientras los términos quepan en el acumulador).
template <typename S>
[[nodiscard]] constexpr S determinant(const Mat<3, S>& a) {
	const S m0 = scalar_traits<S>::norm_from((a.m[1][1] * a.m[2][2]) - (a.m[1][2] * a.m[2][1]));
	const S m1 = scalar_traits<S>::norm_from((a.m[1][0] * a.m[2][2]) - (a.m[1][2] * a.m[2][0]));
	const S m2 = scalar_traits<S>::norm_from((a.m[1][0] * a.m[2][1]) - (a.m[1][1] * a.m[2][0]));
	const auto det = (a.m[0][0] * m0) - (a.m[0][1] * m1) + (a.m[0][2] * m2);
	return scalar_traits<S>::norm_from(det);
}

/// Inversa 3x3 analítica (`adj / det`, regla de Laplace): cada cofactor se normaliza a `S` y
/// se multiplica por `1/det` con `mul_norm`. Como `determinant`, evita productos encadenados;
/// `det = 0` → resultado saturado.
template <typename S>
[[nodiscard]] constexpr Mat<3, S> inverse(const Mat<3, S>& a) {
	const S inv = div_norm(scalar_traits<S>::one(), determinant(a));
	const auto minor = [](S p, S q, S r, S s) {
		return scalar_traits<S>::norm_from((p * q) - (r * s));
	};
	Mat<3, S> m {};
	m.m[0][0] = mul_norm( minor(a.m[1][1], a.m[2][2], a.m[1][2], a.m[2][1]), inv);
	m.m[0][1] = mul_norm(-minor(a.m[0][1], a.m[2][2], a.m[0][2], a.m[2][1]), inv);
	m.m[0][2] = mul_norm( minor(a.m[0][1], a.m[1][2], a.m[0][2], a.m[1][1]), inv);
	m.m[1][0] = mul_norm(-minor(a.m[1][0], a.m[2][2], a.m[1][2], a.m[2][0]), inv);
	m.m[1][1] = mul_norm( minor(a.m[0][0], a.m[2][2], a.m[0][2], a.m[2][0]), inv);
	m.m[1][2] = mul_norm(-minor(a.m[0][0], a.m[1][2], a.m[0][2], a.m[1][0]), inv);
	m.m[2][0] = mul_norm( minor(a.m[1][0], a.m[2][1], a.m[1][1], a.m[2][0]), inv);
	m.m[2][1] = mul_norm(-minor(a.m[0][0], a.m[2][1], a.m[0][1], a.m[2][0]), inv);
	m.m[2][2] = mul_norm( minor(a.m[0][0], a.m[1][1], a.m[0][1], a.m[1][0]), inv);
	return m;
}

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
