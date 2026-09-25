#pragma once

/// \file bit.hpp
/// Manipulación de bits (`eng::util`), equivalente freestanding a `<bit>`.
///
/// El 68000 tiene instrucciones de bits nativas (`btst`/`bset`/`bclr`) y el código
/// de máscaras (Blitter, colisiones, índices de tile, planos, `saveword`) las usa
/// constantemente. Esta cabecera concentra las operaciones que C++ moderno ofrece
/// en `<bit>` (`popcount`, `countl_zero`, `rotl`, `byteswap`, `bit_cast`…) para no
/// reimplementarlas a mano ni depender de la STL.
///
/// Regla de anchura (crítica en este repo): la aritmética de bits debe operar
/// sobre el ancho EXACTO del dato. `eng::u32` es `unsigned long`, que en m68k y en
/// MinGW (host de tests) mide 4 bytes, pero en Linux mide 8. Por eso todo se
/// enmascara primero al ancho real de `T` y los builtins de conteo se corrigen por
/// la diferencia de anchura de `unsigned long`.
///
/// Uso:
///   const int n = eng::util::popcount(tile_mask);      // bits a 1
///   const eng::u32 idx = eng::util::countr_zero(word); // primer bit bajo
///   const eng::u16 flipped = eng::util::bswap16(col);  // endian
///
/// `has_single_bit`/`bit_width` complementan `eng::is_pow2`/`eng::ilog2`
/// (`eng/core/math/fast_div.hpp`), que siguen siendo la forma canónica de detectar y
/// medir potencias de dos en el camino caliente.

#include <eng/core/math/fast_div.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

/// Bit de menos peso de una máscara de un bit (patrón de `saveword`, flags).
template <class T>
[[nodiscard]] constexpr T one_bit() noexcept {
	static_assert(is_integral_v<T>, "one_bit: T no es un entero");
	return static_cast<T>(make_unsigned_t<T> {1});
}

/// Número de bits a 1 del valor, contando en el ancho de `T`.
///
/// SWAR de 32 bits **sin `__builtin_popcount`**: en el m68k-amiga-elf el builtin acaba en
/// `__popcountsi2`, que el libgcc de ese target no enlaza (undefined reference). El
/// `static_cast<U>` zero-extiende, así que los bits altos no cuentan para `T` menor de 32.
template <class T>
[[nodiscard]] constexpr int popcount(T value) noexcept {
	static_assert(is_integral_v<T>, "popcount: T no es un entero");
	static_assert(sizeof(T) <= 4u, "popcount: ancho máximo 32 bits");
	using U = make_unsigned_t<T>;
	eng::u32 x = static_cast<eng::u32>(static_cast<U>(value));
	x = x - ((x >> 1u) & 0x55555555u);
	x = (x & 0x33333333u) + ((x >> 2u) & 0x33333333u);
	x = (x + (x >> 4u)) & 0x0f0f0f0fu;
	x = x + (x >> 8u);
	x = x + (x >> 16u);
	return static_cast<int>(x & 0x3fu);
}

/// Ceros a la izquierda (bits altos a 0) dentro del ancho de `T`. Devuelve el
/// ancho completo si el valor es 0 (donde `__builtin_clz` sería indefinido).
template <class T>
[[nodiscard]] constexpr int countl_zero(T value) noexcept {
	static_assert(is_integral_v<T>, "countl_zero: T no es un entero");
	static_assert(sizeof(T) <= 4u, "countl_zero: ancho máximo 32 bits");
	using U = make_unsigned_t<T>;
	constexpr int width = static_cast<int>(sizeof(T) * 8u);
	constexpr int long_width = static_cast<int>(sizeof(unsigned long) * 8u);
	const unsigned long v = static_cast<unsigned long>(static_cast<U>(value));
	if (v == 0ul) {
		return width;
	}
	return static_cast<int>(__builtin_clzl(v)) - (long_width - width);
}

/// Ceros a la derecha (bits bajos a 0) dentro del ancho de `T`.
template <class T>
[[nodiscard]] constexpr int countr_zero(T value) noexcept {
	static_assert(is_integral_v<T>, "countr_zero: T no es un entero");
	static_assert(sizeof(T) <= 4u, "countr_zero: ancho máximo 32 bits");
	using U = make_unsigned_t<T>;
	constexpr int width = static_cast<int>(sizeof(T) * 8u);
	const unsigned long v = static_cast<unsigned long>(static_cast<U>(value));
	if (v == 0ul) {
		return width;
	}
	return __builtin_ctzl(v);
}

/// Unos a la izquierda dentro del ancho de `T`.
template <class T>
[[nodiscard]] constexpr int countl_one(T value) noexcept {
	using U = make_unsigned_t<T>;
	return countl_zero(static_cast<T>(static_cast<U>(~static_cast<U>(value))));
}

/// Unos a la derecha dentro del ancho de `T`.
template <class T>
[[nodiscard]] constexpr int countr_one(T value) noexcept {
	using U = make_unsigned_t<T>;
	return countr_zero(static_cast<T>(static_cast<U>(~static_cast<U>(value))));
}

/// Número de bits necesarios para representar el valor (`0` para 0).
template <class T>
[[nodiscard]] constexpr int bit_width(T value) noexcept {
	return static_cast<int>(sizeof(T) * 8u) - countl_zero(value);
}

/// ¿El valor tiene exactamente un bit a 1? (complementa `eng::is_pow2` para
/// cualquier entero de hasta 32 bits; `eng::is_pow2` es la forma canónica para u32).
template <class T>
[[nodiscard]] constexpr bool has_single_bit(T value) noexcept {
	static_assert(sizeof(T) <= 4u, "has_single_bit: ancho máximo 32 bits");
	using U = make_unsigned_t<T>;
	return is_pow2(static_cast<u32>(static_cast<U>(value)));
}

/// Potencia de dos más alta que no supera el valor (`0` si el valor es 0).
template <class T>
[[nodiscard]] constexpr T bit_floor(T value) noexcept {
	if (value == 0) {
		return static_cast<T>(0);
	}
	using U = make_unsigned_t<T>;
	return static_cast<T>(static_cast<U>(static_cast<U>(1) << (bit_width(value) - 1)));
}

/// Potencia de dos más baja que no es menor que el valor (mínimo 1). Si el
/// resultado no cabe en `T`, devuelve 0 en vez de un desplazamiento indefinido.
template <class T>
[[nodiscard]] constexpr T bit_ceil(T value) noexcept {
	if (value <= static_cast<T>(1)) {
		return static_cast<T>(1);
	}
	using U = make_unsigned_t<T>;
	constexpr int width = static_cast<int>(sizeof(T) * 8u);
	const int shift = bit_width(static_cast<U>(value - 1));
	if (shift >= width) {
		return static_cast<T>(0);
	}
	return static_cast<T>(static_cast<U>(static_cast<U>(1) << shift));
}

/// Rotación a la izquierda dentro del ancho de `T` (`k` se normaliza módulo ancho).
template <class T>
[[nodiscard]] constexpr T rotl(T value, unsigned k) noexcept {
	static_assert(is_integral_v<T>, "rotl: T no es un entero");
	using U = make_unsigned_t<T>;
	constexpr unsigned w = static_cast<unsigned>(sizeof(T) * 8u);
	const unsigned s = k & (w - 1u);
	const U v = static_cast<U>(value);
	return static_cast<T>(static_cast<U>((v << s) | (v >> ((w - s) & (w - 1u)))));
}

/// Rotación a la derecha dentro del ancho de `T`.
template <class T>
[[nodiscard]] constexpr T rotr(T value, unsigned k) noexcept {
	static_assert(is_integral_v<T>, "rotr: T no es un entero");
	using U = make_unsigned_t<T>;
	constexpr unsigned w = static_cast<unsigned>(sizeof(T) * 8u);
	const unsigned s = k & (w - 1u);
	const U v = static_cast<U>(value);
	return static_cast<T>(static_cast<U>((v >> s) | (v << ((w - s) & (w - 1u)))));
}

/// Invierte el orden de los dos bytes de una palabra (`swap` del 68000).
[[nodiscard]] constexpr u16 bswap16(u16 value) noexcept {
	return static_cast<u16>((value << 8u) | (value >> 8u));
}

/// Invierte el orden de los cuatro bytes de una palabra larga.
[[nodiscard]] constexpr u32 bswap32(u32 value) noexcept {
	return ((value & 0x000000fful) << 24u) | ((value & 0x0000ff00ul) << 8u) |
	       ((value & 0x00ff0000ul) >> 8u) | ((value & 0xff000000ul) >> 24u);
}

/// Reinterpreta el patrón de bits de un tipo como otro del MISMO tamaño, sin
/// violar el aliasing. Requiere tipos copiables trivialmente (p. ej. `u32`↔`float`).
#if defined(__has_builtin)
#if __has_builtin(__builtin_bit_cast)
template <class To, class From>
[[nodiscard]] constexpr To bit_cast(const From& src) noexcept {
	static_assert(sizeof(To) == sizeof(From), "bit_cast: los tipos deben medir lo mismo");
	static_assert(is_trivially_copyable_v<To> && is_trivially_copyable_v<From>,
		      "bit_cast: solo tipos copiables trivialmente");
	return __builtin_bit_cast(To, src);
}
#endif
#endif

} // namespace eng::util
