#pragma once

/// \file type_traits.hpp
/// Rasgos de tipo mínimos del engine (`eng::util`), sin `<type_traits>`.
///
/// El runtime Amiga es freestanding (`-nostdlib`, sin STL hosted) y no puede usar
/// `std::is_same`, `std::enable_if`, etc. Esta cabecera aporta el subconjunto que
/// necesitan las utilidades genéricas del engine (contenedores de capacidad fija,
/// algoritmos sobre `Span`, `Optional`/`Expected`): comparar tipos, quitar
/// `cv`/referencia, elegir un tipo por condición y habilitar sobrecargas.
///
/// Todo es `constexpr` y no genera código: son metadatos de compilación. Los
/// rasgos que el compilador ya resuelve de forma nativa se apoyan en sus builtins
/// (`__is_integral`, `__is_trivially_copyable`…), que en GCC/m68k son exactos.
///
/// Uso:
///   static_assert(eng::util::is_same_v<eng::u32, unsigned long>);
///   template <class T, eng::util::enable_if_t<eng::util::is_integral_v<T>, int> = 0>
///   constexpr T twice(T x) { return x + x; }

#include <eng/core/types.hpp>

namespace eng::util {

/// Constante integral con tipo: base de todos los rasgos.
template <class T, T V>
struct integral_constant {
	static constexpr T value = V;
	using value_type = T;
	using type = integral_constant;
	constexpr operator value_type() const noexcept { return V; }
	[[nodiscard]] constexpr value_type operator()() const noexcept { return V; }
};

/// `true`/`false` como tipo (patrón de etiqueta de los rasgos).
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

template <bool B>
using bool_constant = integral_constant<bool, B>;

// --- Identidad --------------------------------------------------------------

template <class T, class U>
struct is_same : false_type {};
template <class T>
struct is_same<T, T> : true_type {};
template <class T, class U>
inline constexpr bool is_same_v = is_same<T, U>::value;

// --- Cualificadores y referencias -------------------------------------------

template <class T>
struct remove_const {
	using type = T;
};
template <class T>
struct remove_const<const T> {
	using type = T;
};
template <class T>
struct remove_volatile {
	using type = T;
};
template <class T>
struct remove_volatile<volatile T> {
	using type = T;
};
template <class T>
struct remove_cv {
	using type = typename remove_volatile<typename remove_const<T>::type>::type;
};

template <class T>
struct remove_reference {
	using type = T;
};
template <class T>
struct remove_reference<T&> {
	using type = T;
};
template <class T>
struct remove_reference<T&&> {
	using type = T;
};

template <class T>
using remove_const_t = typename remove_const<T>::type;
template <class T>
using remove_volatile_t = typename remove_volatile<T>::type;
template <class T>
using remove_cv_t = typename remove_cv<T>::type;
template <class T>
using remove_reference_t = typename remove_reference<T>::type;
/// Quita `cv` y referencia de una sola vez (lo que suele querer un parámetro por valor).
template <class T>
using remove_cvref_t = remove_cv_t<remove_reference_t<T>>;

template <class T>
struct is_lvalue_reference : false_type {};
template <class T>
struct is_lvalue_reference<T&> : true_type {};
template <class T>
inline constexpr bool is_lvalue_reference_v = is_lvalue_reference<T>::value;

template <class T>
struct is_rvalue_reference : false_type {};
template <class T>
struct is_rvalue_reference<T&&> : true_type {};
template <class T>
inline constexpr bool is_rvalue_reference_v = is_rvalue_reference<T>::value;

// --- Punteros ---------------------------------------------------------------

template <class T>
struct remove_pointer {
	using type = T;
};
template <class T>
struct remove_pointer<T*> {
	using type = T;
};
template <class T>
struct remove_pointer<T* const> {
	using type = T;
};
template <class T>
struct remove_pointer<T* volatile> {
	using type = T;
};
template <class T>
using remove_pointer_t = typename remove_pointer<T>::type;

// --- Selección y habilitación ----------------------------------------------

/// Elige `T` si `B`, `F` si no.
template <bool B, class T, class F>
struct conditional {
	using type = T;
};
template <class T, class F>
struct conditional<false, T, F> {
	using type = F;
};
template <bool B, class T, class F>
using conditional_t = typename conditional<B, T, F>::type;

/// SFINAE de sobrecargas: `enable_if_t<cond, T>` solo existe si `cond` es `true`.
template <bool B, class T = void>
struct enable_if {};
template <class T>
struct enable_if<true, T> {
	using type = T;
};
template <bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;

template <class...>
using void_t = void;

// --- Categorías de tipo -----------------------------------------------------
//
// GCC no expone `__is_integral`/`__is_arithmetic` como builtins (sí
// `__is_enum`, `__is_class`, `__is_trivially_copyable`…), así que las categorías
// básicas se especializan a mano. Es la misma lista que usan las utilidades
// genéricas del engine y no depende de `<type_traits>`.

template <class T>
struct is_integral : false_type {};
template <>
struct is_integral<bool> : true_type {};
template <>
struct is_integral<char> : true_type {};
template <>
struct is_integral<signed char> : true_type {};
template <>
struct is_integral<unsigned char> : true_type {};
template <>
struct is_integral<short> : true_type {};
template <>
struct is_integral<unsigned short> : true_type {};
template <>
struct is_integral<int> : true_type {};
template <>
struct is_integral<unsigned int> : true_type {};
template <>
struct is_integral<long> : true_type {};
template <>
struct is_integral<unsigned long> : true_type {};
template <>
struct is_integral<long long> : true_type {};
template <>
struct is_integral<unsigned long long> : true_type {};
template <>
struct is_integral<wchar_t> : true_type {};
template <>
struct is_integral<char16_t> : true_type {};
template <>
struct is_integral<char32_t> : true_type {};
#if defined(__cpp_char8_t)
template <>
struct is_integral<char8_t> : true_type {};
#endif
template <class T>
inline constexpr bool is_integral_v = is_integral<T>::value;

template <class T>
struct is_floating_point : false_type {};
template <>
struct is_floating_point<float> : true_type {};
template <>
struct is_floating_point<double> : true_type {};
template <>
struct is_floating_point<long double> : true_type {};
template <class T>
inline constexpr bool is_floating_point_v = is_floating_point<T>::value;

template <class T>
struct is_arithmetic : bool_constant<is_integral_v<T> || is_floating_point_v<T>> {};
template <class T>
inline constexpr bool is_arithmetic_v = is_arithmetic<T>::value;

namespace detail {

template <class T, class = void>
struct signed_impl : false_type {};
template <class T>
struct signed_impl<T, enable_if_t<is_arithmetic_v<T>>> : bool_constant<(T(-1) < T(0))> {};

template <class T, class = void>
struct unsigned_impl : false_type {};
template <class T>
struct unsigned_impl<T, enable_if_t<is_integral_v<T>>> : bool_constant<!((T)(-1) < (T)0)> {};

} // namespace detail

template <class T>
struct is_signed : detail::signed_impl<T> {};
template <class T>
inline constexpr bool is_signed_v = is_signed<T>::value;

template <class T>
struct is_unsigned : detail::unsigned_impl<T> {};
template <class T>
inline constexpr bool is_unsigned_v = is_unsigned<T>::value;

template <class T>
struct is_pointer : bool_constant<__is_pointer(T)> {};
template <class T>
inline constexpr bool is_pointer_v = is_pointer<T>::value;

template <class T>
struct is_enum : bool_constant<__is_enum(T)> {};
template <class T>
inline constexpr bool is_enum_v = is_enum<T>::value;

template <class T>
struct is_class : bool_constant<__is_class(T)> {};
template <class T>
inline constexpr bool is_class_v = is_class<T>::value;

template <class T>
struct is_function : bool_constant<__is_function(T)> {};
template <class T>
inline constexpr bool is_function_v = is_function<T>::value;

/// Un tipo copiable con `memcpy` (memoria y valor idénticos): los contenedores de
/// capacidad fija del engine lo usan para decidir entre copia trivial y por elemento.
template <class T>
struct is_trivially_copyable : bool_constant<__is_trivially_copyable(T)> {};
template <class T>
inline constexpr bool is_trivially_copyable_v = is_trivially_copyable<T>::value;

template <class T>
struct is_trivially_destructible : bool_constant<__has_trivial_destructor(T)> {};
template <class T>
inline constexpr bool is_trivially_destructible_v = is_trivially_destructible<T>::value;

template <class T>
struct is_trivially_default_constructible : bool_constant<__has_trivial_constructor(T)> {};
template <class T>
inline constexpr bool is_trivially_default_constructible_v =
	is_trivially_default_constructible<T>::value;

// --- Entero sin signo del mismo ancho ---------------------------------------

namespace detail {

/// Entero sin signo de exactamente `Bytes` bytes (1, 2 o 4). El ancho exacto
/// importa en 68000: la aritmética de bits debe operar sobre 16/32 bits reales y
/// no sobre el `unsigned long` (4 bytes en m68k y Windows, 8 en Linux).
template <unsigned Bytes>
struct uint_of;
template <>
struct uint_of<1> {
	using type = __UINT8_TYPE__;
};
template <>
struct uint_of<2> {
	using type = __UINT16_TYPE__;
};
template <>
struct uint_of<4> {
	using type = __UINT32_TYPE__;
};

} // namespace detail

/// Representación sin signo del MISMO ancho que `T` (falla al instanciar si `T`
/// no es entero de 1, 2 o 4 bytes; `bool` se trata como 1 byte).
template <class T>
struct make_unsigned {
	static_assert(is_integral_v<T>, "make_unsigned: T no es un entero");
	using type = typename detail::uint_of<sizeof(T)>::type;
};
template <class T>
using make_unsigned_t = typename make_unsigned<T>::type;

/// Tipo subyacente de un `enum` (para serializar o indexar por su valor).
template <class T>
struct underlying_type {
	static_assert(is_enum_v<T>, "underlying_type: T no es un enum");
	using type = __underlying_type(T);
};
template <class T>
using underlying_type_t = typename underlying_type<T>::type;

/// Convierte un `enum class` a su valor entero (equivalente a `std::to_underlying`).
template <class T>
[[nodiscard]] constexpr underlying_type_t<T> to_underlying(T value) noexcept {
	return static_cast<underlying_type_t<T>>(value);
}

} // namespace eng::util
