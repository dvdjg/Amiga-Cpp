#pragma once

/// \file expr.hpp
/// **Expression templates lite** (`eng::math::et`): construye un árbol de expresión en
/// tiempo de compilación y lo evalúa **una sola vez**, al convertir al tipo final. Evita
/// los temporales que crea cada operador suelto (`a + b*c - d` materializaría tres
/// valores intermedios) y deja al optimizador ver la expresión entera para mantener
/// operandos en registros. En un 68000 el objetivo no es "menos líneas" sino **menos
/// construcciones/destrucciones** de escalares caros (`Fixed`, `MiniFloat16`) y mejor
/// presión de registros.
///
/// ## Qué es y qué no es
///
/// - Es **lite**: sin heap, sin virtuales, sin STL, header-only, `constexpr`. Un nodo
///   guarda sus operandos **por valor** (los escalares del engine son pequeños), así que
///   no hay punteros ni ciclos de vida que gestionar.
/// - Es **explícito**: se construye el árbol con `val(x)`; la evaluación es *eager* al
///   llamar `.eval<T>()` (o `evaluate<T>(expr)`). No hay conversiones implícitas ni
///   sobrecarga de `operator=` que pueda disparar evaluación por sorpresa.
/// - Es **genérico**: el mismo mecanismo sirve a cualquier tipo con `+`, `-`, `*` (y `/`
///   si lo tiene). Funciona con `Fixed`, `MiniFloat16`, `float`/`double` y también con
///   **contenedores** (`Vec<N,S>`, `Mat<N,S>`): `eval_into(dst, expr)` fusiona la
///   expresión **componente a componente** en un solo bucle, que es donde el álgebra
///   matricial se beneficia de verdad.
///
/// ## Uso (escalares)
///
/// ```cpp
/// using namespace eng::math;
/// using namespace eng::math::et;
/// Fixed<s16, 12> a{...}, b{...}, c{...}, d{...};
/// const Fixed<s16, 12> r = evaluate<Fixed<s16, 12>>(val(a) + val(b) * val(c) - val(d));
/// ```
///
/// ## Uso (cálculo matricial, evaluación fusionada)
///
/// ```cpp
/// Vec<3, Fixed<s16, 12>> p{...}, v{...}, out{};
/// et::eval_into(out, et::val(p) + et::val(v) * half);   // un solo bucle, sin Vec temporal
/// ```
///
/// ## Protocolo de un nodo
///
/// Todo nodo deriva de `Expr<Derived>` y expone:
///
/// - `value()`: el valor **completo** del subárbol (para el resultado escalar o para un
///   contenedor de una sola pieza).
/// - `at(i)`: el **componente `i`** del subárbol. Una hoja escalar lo ignora (broadcast);
///   una hoja contenedor devuelve su componente; un nodo binario compone.
///
/// Un contenedor participa si define `et_get(c, i)` y `et_count(c)` (hay implementaciones
/// para `Vec`/`Mat` más abajo); `et_set(dst, i, x)` escribe un componente del destino.
/// Añadir un contenedor nuevo es definir esos tres puntos, nada más.
///
/// ## Límites
///
/// - Los operandos deben ser del **mismo escalar** (igual que `eng::math::linalg`): el
///   árbol no mezcla `float` con `Fixed`; eso sigue siendo un error de dominio. Un literal
///   crudo (`2`) tampoco promociona a `Fixed`: usa `Fixed<...>::from_int(2)` o una Q.
/// - El producto de `Fixed` cambia de exponente: `eval<S>` convierte al tipo destino con
///   `converter<S>` (para `Fixed` es `rescale`+`cast`+`retag`). Dentro de la expresión, `+`
///   y `-` usan `et_add`/`et_sub`, que **promueven** los dos operandos al tipo común ancho
///   (mayor exponente y representación común) antes de sumar: así `a + b*c` compila, se
///   acumula exacto en el exponente del producto y se normaliza **una sola vez** al
///   evaluar. Subir exponente es un desplazamiento exacto; si el valor no cupiera en la
///   representación común, la política del escalar decide al convertir al destino.
/// - No sustituye al álgebra escrita a mano para expresiones **muy** largas ni a elegir
///   menos operaciones o tablas: mide con el profiler antes de darlo por ganancia.

#include <eng/core/types/types.hpp>

namespace eng::math {

// Declaraciones de los contenedores genéricos (definidos en `linalg.hpp`) y del escalar
// `Fixed` (definido en `fixed.hpp`) para poder ofrecer la fusión por componente y la
// conversión de exponente sin que esta cabecera dependa del álgebra ni del fixed.
template <int N, typename S>
struct Vec;
template <int N, typename S>
struct Mat;
template <typename R, int Exp, typename Policy>
struct Fixed;
template <typename A, typename B>
struct common_repr;

namespace et {

// ============================================================================
//  Conversión al tipo destino
// ============================================================================

/// Conversión de un valor al tipo destino `S`. Por defecto, el `static_cast` del
/// lenguaje, que vale para `float`, `double`, `MiniFloat16` y cualquier tipo convertible.
///
/// La conversión vive en una **clase-trait** (no en una sobrecarga de función) para que
/// pueda **especializarse parcialmente** sobre un tipo con plantilla (como
/// `Fixed<R,E,P>`); una sobrecarga de función no puede deducir `E` desde un argumento
/// explícito `et_cast<Fixed<R,E,P>>`.
template <typename S>
struct converter {
	template <typename V>
	[[nodiscard]] static constexpr S from(const V& v) {
		return static_cast<S>(v);
	}
};

/// `Fixed`: un `static_cast` no cambia de exponente, así que se **reescala** al exponente
/// destino, se **moldea** a su representación y se **re-etiqueta** su política. El orden
/// importa: al **subir** exponente se ensancha primero la representación (`cast`) y luego
/// se desplaza a la izquierda; al **bajar**, se desplaza con redondeo primero y luego se
/// moldea. Hacerlo al revés desbordaría la representación estrecha antes de ensancharla.
template <typename R2, int E2, typename P2>
struct converter<Fixed<R2, E2, P2>> {
	template <typename R, int E1, typename P1>
	[[nodiscard]] static constexpr Fixed<R2, E2, P2> from(const Fixed<R, E1, P1>& v) {
		if constexpr (E2 >= E1) {
			return v.template cast<R2>().template rescale<E2>().template retag<P2>();
		} else {
			return v.template rescale<E2>().template cast<R2>().template retag<P2>();
		}
	}
};

// ============================================================================
//  Suma/resta con acumulador ancho (punto de personalización por escalar)
// ============================================================================

/// `+` y `-` **dentro de una expresión**. Por defecto, el operador del tipo (para `float`,
/// `MiniFloat16`, `Vec`, `Mat`…). Un escalar que cambia de exponente al operar (como
/// `Fixed`) redefine `et_add`/`et_sub` para **promover** los dos operandos al tipo común
/// más ancho antes de sumar: así `a + b*c` (término en `E` más producto en `2E`) compila y
/// la normalización ocurre **una sola vez**, al convertir el resultado al tipo destino.
template <typename A, typename B>
[[nodiscard]] constexpr auto et_add(const A& a, const B& b) {
	return a + b;
}
template <typename A, typename B>
[[nodiscard]] constexpr auto et_sub(const A& a, const B& b) {
	return a - b;
}

/// Tipo común de dos `Fixed` para sumar/restar en una expresión: la representación común
/// (que en un producto ya es la ancha, p. ej. `s32`) y el **mayor** exponente.
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
using et_fixed_sum_t = Fixed<typename common_repr<Ra, Rb>::type, (Ea > Eb ? Ea : Eb), P>;

/// `Fixed + Fixed` en una expresión: ambos operandos se convierten al tipo común (subir
/// exponente es un desplazamiento **exacto**; la representación se ensancha) y se suman ahí.
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
[[nodiscard]] constexpr et_fixed_sum_t<Ra, Ea, Rb, Eb, P> et_add(const Fixed<Ra, Ea, P>& a,
								 const Fixed<Rb, Eb, P>& b) {
	using T = et_fixed_sum_t<Ra, Ea, Rb, Eb, P>;
	return converter<T>::from(a) + converter<T>::from(b);
}
template <typename Ra, int Ea, typename Rb, int Eb, typename P>
[[nodiscard]] constexpr et_fixed_sum_t<Ra, Ea, Rb, Eb, P> et_sub(const Fixed<Ra, Ea, P>& a,
								 const Fixed<Rb, Eb, P>& b) {
	using T = et_fixed_sum_t<Ra, Ea, Rb, Eb, P>;
	return converter<T>::from(a) - converter<T>::from(b);
}

/// Convierte `v` al tipo `S` (paso final de `.eval<S>()` y de la escritura por componente).
template <typename S, typename V>
[[nodiscard]] constexpr S et_cast(const V& v) {
	return converter<S>::from(v);
}

// ============================================================================
//  Acceso por componente (punto de personalización para contenedores)
// ============================================================================

/// Componente `i` de un `Vec` (acceso contiguo del propio tipo).
template <int N, typename S>
[[nodiscard]] constexpr S et_get(const Vec<N, S>& v, int i) {
	return v[i];
}
/// Componente `i` de un `Mat`, en orden **por filas** (`i = fila*N + columna`).
template <int N, typename S>
[[nodiscard]] constexpr S et_get(const Mat<N, S>& m, int i) {
	return m.m[i / N][i % N];
}
/// Número de componentes de un contenedor.
template <int N, typename S>
[[nodiscard]] constexpr int et_count(const Vec<N, S>&) {
	return N;
}
template <int N, typename S>
[[nodiscard]] constexpr int et_count(const Mat<N, S>&) {
	return N * N;
}

/// Escribe el componente `i` de un contenedor, convirtiendo al escalar del destino.
template <int N, typename S, typename V>
constexpr void et_set(Vec<N, S>& v, int i, const V& x) {
	v[i] = et_cast<S>(x);
}
template <int N, typename S, typename V>
constexpr void et_set(Mat<N, S>& m, int i, const V& x) {
	m.m[i / N][i % N] = et_cast<S>(x);
}

/// ¿`T` es un contenedor indexable para expression templates?
template <typename T>
concept Indexed = requires(const T& t, int i) {
	{ et_get(t, i) };
	{ et_count(t) };
};

// ============================================================================
//  Nodos
// ============================================================================

/// Evalúa `e` **dentro** de `dst` componente a componente (declarada aquí para que
/// `Expr::assign_to` la encuentre).
template <typename Dst, typename E>
[[gnu::always_inline]] constexpr void eval_into(Dst& dst, const E& e);

/// Base CRTP de todo nodo. Aporta la evaluación y la asignación fusionada.
template <typename Derived>
struct Expr {
	using expr_node = void;

	[[nodiscard]] constexpr const Derived& self() const {
		return static_cast<const Derived&>(*this);
	}

	/// Evalúa **toda** la expresión al tipo `S` (escalar o contenedor).
	template <typename S>
	[[nodiscard, gnu::always_inline]] constexpr S eval() const {
		return et_cast<S>(self().value());
	}

	/// Evalúa la expresión **dentro** del contenedor `dst`, componente a componente,
	/// reutilizando el bucle de `eval_into` (sin construir un contenedor temporal).
	template <typename Dst>
	constexpr void assign_to(Dst& dst) const {
		eval_into(dst, self());
	}
};

/// ¿`T` es un nodo de expresión? (lo marca la herencia de `Expr`, vía `expr_node`).
template <typename T>
concept IsExpr = requires { typename T::expr_node; };

/// Hoja: un valor escalar o un contenedor, guardado por valor.
template <typename T>
struct Val : Expr<Val<T>> {
	T v;

	constexpr explicit Val(T x) : v(x) {}

	[[nodiscard]] constexpr const T& value() const { return v; }

	/// Componente `i`: si `T` es contenedor, su componente; si es escalar, se difunde
	/// (`broadcast`) para poder llenar un contenedor con una constante.
	[[nodiscard, gnu::always_inline]] constexpr auto at(int i) const {
		if constexpr (Indexed<T>) {
			return et_get(v, i);
		} else {
			(void)i;
			return v;
		}
	}
};

/// Etiquetas de operación: cada una sabe aplicar su operador. Al ser genéricas, sirven
/// tanto para escalares como para contenedores (y, en `at`, para sus componentes).
struct Add {
	template <typename A, typename B>
	static constexpr auto apply(const A& a, const B& b) {
		return et_add(a, b);
	}
};
struct Sub {
	template <typename A, typename B>
	static constexpr auto apply(const A& a, const B& b) {
		return et_sub(a, b);
	}
};
struct Mul {
	template <typename A, typename B>
	static constexpr auto apply(const A& a, const B& b) {
		return a * b;
	}
};
struct Div {
	template <typename A, typename B>
	static constexpr auto apply(const A& a, const B& b) {
		return a / b;
	}
};

/// Nodo binario: `Op(lhs, rhs)`.
template <typename Op, typename L, typename R>
struct Bin : Expr<Bin<Op, L, R>> {
	L lhs;
	R rhs;

	constexpr Bin(L l, R r) : lhs(l), rhs(r) {}

	[[nodiscard, gnu::always_inline]] constexpr auto value() const { return Op::apply(lhs.value(), rhs.value()); }
	[[nodiscard, gnu::always_inline]] constexpr auto at(int i) const { return Op::apply(lhs.at(i), rhs.at(i)); }
};

/// Nodo unario de negación.
template <typename L>
struct Negate : Expr<Negate<L>> {
	L lhs;

	constexpr explicit Negate(L l) : lhs(l) {}

	[[nodiscard, gnu::always_inline]] constexpr auto value() const { return -lhs.value(); }
	[[nodiscard, gnu::always_inline]] constexpr auto at(int i) const { return -lhs.at(i); }
};

// ============================================================================
//  Construcción del árbol
// ============================================================================

/// Envuelve un valor como hoja; si ya es una expresión, la devuelve tal cual. Es el
/// único punto que hay que recordar para construir un árbol.
template <typename T>
[[nodiscard]] constexpr auto val(T x) {
	if constexpr (IsExpr<T>) {
		return x;
	} else {
		return Val<T> {x};
	}
}

template <typename L, typename R>
[[nodiscard]] constexpr auto operator+(const Expr<L>& a, const Expr<R>& b) {
	return Bin<Add, L, R> {a.self(), b.self()};
}
template <typename L, typename R>
[[nodiscard]] constexpr auto operator-(const Expr<L>& a, const Expr<R>& b) {
	return Bin<Sub, L, R> {a.self(), b.self()};
}
template <typename L, typename R>
[[nodiscard]] constexpr auto operator*(const Expr<L>& a, const Expr<R>& b) {
	return Bin<Mul, L, R> {a.self(), b.self()};
}
template <typename L, typename R>
[[nodiscard]] constexpr auto operator/(const Expr<L>& a, const Expr<R>& b) {
	return Bin<Div, L, R> {a.self(), b.self()};
}
template <typename L>
[[nodiscard]] constexpr auto operator-(const Expr<L>& a) {
	return Negate<L> {a.self()};
}

// Mezcla con valores crudos (Fixed, MiniFloat16, int, float, Vec, Mat…).
template <typename L, typename V>
	requires(!IsExpr<V>)
[[nodiscard]] constexpr auto operator+(const Expr<L>& a, const V& b) {
	return Bin<Add, L, Val<V>> {a.self(), Val<V> {b}};
}
template <typename V, typename R>
	requires(!IsExpr<V>)
[[nodiscard]] constexpr auto operator+(const V& a, const Expr<R>& b) {
	return Bin<Add, Val<V>, R> {Val<V> {a}, b.self()};
}
template <typename L, typename V>
	requires(!IsExpr<V>)
[[nodiscard]] constexpr auto operator-(const Expr<L>& a, const V& b) {
	return Bin<Sub, L, Val<V>> {a.self(), Val<V> {b}};
}
template <typename V, typename R>
	requires(!IsExpr<V>)
[[nodiscard]] constexpr auto operator-(const V& a, const Expr<R>& b) {
	return Bin<Sub, Val<V>, R> {Val<V> {a}, b.self()};
}
template <typename L, typename V>
	requires(!IsExpr<V>)
[[nodiscard]] constexpr auto operator*(const Expr<L>& a, const V& b) {
	return Bin<Mul, L, Val<V>> {a.self(), Val<V> {b}};
}
template <typename V, typename R>
	requires(!IsExpr<V>)
[[nodiscard]] constexpr auto operator*(const V& a, const Expr<R>& b) {
	return Bin<Mul, Val<V>, R> {Val<V> {a}, b.self()};
}
template <typename L, typename V>
	requires(!IsExpr<V>)
[[nodiscard]] constexpr auto operator/(const Expr<L>& a, const V& b) {
	return Bin<Div, L, Val<V>> {a.self(), Val<V> {b}};
}
template <typename V, typename R>
	requires(!IsExpr<V>)
[[nodiscard]] constexpr auto operator/(const V& a, const Expr<R>& b) {
	return Bin<Div, Val<V>, R> {Val<V> {a}, b.self()};
}

// ============================================================================
//  Evaluación
// ============================================================================

template <typename Dst, typename E>
[[gnu::always_inline]] constexpr void eval_into(Dst& dst, const E& e) {
	const int n = et_count(dst);
	for (int i = 0; i < n; ++i) {
		et_set(dst, i, e.at(i));
	}
}

/// Evalúa `e` al tipo `S` (atajo de `e.template eval<S>()`).
template <typename S, typename E>
[[nodiscard]] constexpr S evaluate(const E& e) {
	return e.template eval<S>();
}

} // namespace et
} // namespace eng::math
