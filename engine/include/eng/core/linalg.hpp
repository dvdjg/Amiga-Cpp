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

/// Coma flotante de 64 bits: mismos rasgos que `float` (el álgebra es agnóstica del
/// ancho). Necesario para usar `Vec`/`Mat`/`interp`/`geometry` con `double` en host.
template <>
struct scalar_traits<double> {
	using scalar = double;

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

/// Coma flotante de 16 bits (`MiniFloat16`): el producto ya vive en el mismo espacio
/// (no hay exponente separado que normalizar), así que los rasgos son los del cuerpo.
/// Sólo hay que fijar el `uno` real (por defecto la plantilla primaria usaría `S{1}`,
/// que en este formato es un valor denormal, no 1.0).
template <>
struct scalar_traits<MiniFloat16> {
	using scalar = MiniFloat16;

	static constexpr MiniFloat16 inner(MiniFloat16 a, MiniFloat16 b) { return a * b; }

	static constexpr MiniFloat16 zero() { return MiniFloat16::zero(); }
	static constexpr MiniFloat16 one() { return MiniFloat16::one(); }

	/// Multiply-accumulate de un solo redondeo (FMA): lo usan `Mat*Mat`/`Mat*Vec`.
	static constexpr MiniFloat16 mac(MiniFloat16 a, MiniFloat16 b, MiniFloat16 acc) {
		return mul_add(a, b, acc);
	}

	/// Entero -> MF sin `float` (construcción por bits): en 68000 un `(float)i`
	/// arrastraría `__floatsisf`. Exacto hasta 2048; por encima, redondeo de mantisa.
	static constexpr MiniFloat16 from_int(int i) {
		if (i == 0) return MiniFloat16::zero();
		const bool neg = i < 0;
		eng::u32 a = static_cast<eng::u32>(neg ? -i : i);
		int msb = 0;
		while ((a >> (msb + 1)) != 0u) ++msb;
		const int e = msb + MiniFloat16::bias;
		if (e >= MiniFloat16::exp_inf)
			return MiniFloat16::from_raw(static_cast<eng::u16>(
				(neg ? MiniFloat16::sign_mask : 0u) | MiniFloat16::exp_mask));
		eng::u16 mant;
		if (msb > 10)
			mant = static_cast<eng::u16>((a >> (msb - 10)) & 0x3FFu);
		else
			mant = static_cast<eng::u16>((a << (10 - msb)) & 0x3FFu);
		return MiniFloat16::from_raw(static_cast<eng::u16>(
			(neg ? MiniFloat16::sign_mask : 0u) | (static_cast<eng::u16>(e) << 10) | mant));
	}

	/// MF -> entero truncando hacia cero, sin `float`; satura fuera de `s16`.
	static constexpr int to_int(MiniFloat16 x) {
		const int e = static_cast<int>((x.raw >> 10) & 31) - MiniFloat16::bias;
		if (e < 0) return 0;
		if (e > 14) return (x.raw & MiniFloat16::sign_mask) != 0u ? -32767 : 32767;
		const int mant = 0x400 | (x.raw & MiniFloat16::man_mask);
		const int v = (e <= 10) ? (mant >> (10 - e)) : (mant << (e - 10));
		return (x.raw & MiniFloat16::sign_mask) != 0u ? -v : v;
	}

	template <typename Prod>
	static constexpr MiniFloat16 norm_from(Prod p) {
		return static_cast<MiniFloat16>(p);
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

/// División explícita para fixed 4.12/8.8 (`s16`): `raw = (a.v << E) / b.v`, saturada.
/// Evita el silencio del `operator/` ausente sin abrir la puerta a divisiones
/// implícitas. Usa `arith<s16>::div` (en 68000, `divs.w` nativo 32/16) tras comprobar
/// que el cociente cabe en `s16` (`divs.w` desborda en silencio si no). Válida para
/// `E <= 15` (el intermedio `a.v << E` cabe en `s32`).
template <int E, typename P>
struct scalar_div<Fixed<s16, E, P>> {
	using S = Fixed<s16, E, P>;
	[[nodiscard]] static constexpr S op(S a, S b) {
		constexpr eng::s32 mx = 32767;
		constexpr eng::s32 mn = -32768;
		if (b.v == 0) return S {static_cast<eng::s16>(a.v < 0 ? mn : mx)}; // satura con signo
		const eng::s32 num = static_cast<eng::s32>(a.v) << E;
		const eng::s32 den = b.v;
		const eng::s32 lim = mx * (den < 0 ? -den : den); // |num| <= lim  =>  cociente en s16
		if (num > lim) return S {static_cast<eng::s16>(mx)};
		if (num < -lim) return S {static_cast<eng::s16>(mn)};
		return S {arith<s16>::div(num, static_cast<eng::s16>(den))}; // divs.w nativo
	}
};

/// División de `Fixed<s32,E>` (32 bits): el intermedio `a.v·2^E` no cabe en 32 bits, así
/// que usa `s64`. **No disponible en m68k** (la aritmética de 64 bits son libcalls de
/// libgcc): allí usa `Fixed<s16,E>`. En host/32 bits nativo es una división de máquina.
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
		const eng::s64 num =
			static_cast<eng::s64>(a.v) * (static_cast<eng::s64>(1) << E);
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
