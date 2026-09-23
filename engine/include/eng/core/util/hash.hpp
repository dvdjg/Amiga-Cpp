#pragma once

/// \file hash.hpp
/// **Funciones hash** del engine (`eng::util`), pensadas para el 68000.
///
/// El 68000 no multiplica dos enteros de 32 bits con una instrucción: `a * b` sobre
/// `u32` acaba en `__mulsi3` (libcall, ~150 ciclos). Los hashes clásicos (FNV-1a,
/// splitmix) usan precisamente esa multiplicación. Aquí se evitan:
///
/// - enteros de 16 bits o menos: **un `mulu.w`** (16×16→32, nativo) + mezcla;
/// - enteros de 32 bits y punteros: mezcla de **rotaciones, xors y sumas** (sin mul);
/// - cadenas: `h = h·33 + byte` con `h<<5 + h` (shift + suma), sin multiplicar.
///
/// El hash no pretende resistencia criptográfica: solo dispersar claves para una
/// tabla de sondeo lineal. La calidad se comprueba con tests host (colisiones) y el
/// gate de codegen exige que no aparezca `__mulsi3` ni instrucciones de 68020.
///
/// Uso:
///   const eng::u32 h = eng::util::hash_value(entity_id);
///   const eng::u32 s = eng::util::hash_string(name);

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/bit.hpp>
#include <eng/core/util/string_view.hpp>
#include <eng/core/util/type_traits.hpp>
#include <eng/core/math/arith.hpp>

namespace eng::util {

/// Avalancha de 32 bits sin multiplicación: rota, xors y sumas encadenadas.
[[nodiscard]] constexpr u32 hash_u32(u32 x) noexcept {
	x ^= x >> 16u;
	x = static_cast<u32>(rotl(x, 13u) + (x >> 7u));
	x ^= x >> 17u;
	x ^= x >> 11u;
	return x;
}

/// Hash de 16 bits con un único `mulu.w` (constante de Knuth) y avalancha.
[[nodiscard]] constexpr u32 hash_u16(u16 x) noexcept {
	const u32 h = eng::math::mulu16(x, 40503u);
	return hash_u32(h ^ (h >> 16u));
}

/// Hash de un byte: dispersa el valor para que bits bajos no colisionen.
[[nodiscard]] constexpr u32 hash_u8(u8 x) noexcept {
	return hash_u16(static_cast<u16>(x) * 257u);
}

/// Hash de un entero. Usa `mulu.w` en anchos de 16 bits o menos.
template <class T>
	requires(is_integral_v<T>)
[[nodiscard]] constexpr u32 hash_value(T x) noexcept {
	using U = make_unsigned_t<T>;
	const U u = static_cast<U>(x);
	if constexpr (sizeof(T) <= 1u) {
		return hash_u8(static_cast<u8>(u));
	} else if constexpr (sizeof(T) <= 2u) {
		return hash_u16(static_cast<u16>(u));
	} else {
		return hash_u32(static_cast<u32>(u));
	}
}

/// Hash de un `enum` (por su valor entero).
template <class E>
	requires(is_enum_v<E>)
[[nodiscard]] constexpr u32 hash_value(E value) noexcept {
	return hash_value(static_cast<underlying_type_t<E>>(value));
}

/// Hash de un puntero (por su dirección).
template <class T>
[[nodiscard]] constexpr u32 hash_value(T* p) noexcept {
	return hash_u32(static_cast<u32>(reinterpret_cast<uintptr>(p)));
}

/// Hash de una secuencia de bytes: `h = h·33 + byte` (sin multiplicar) + mezcla.
[[nodiscard]] constexpr u32 hash_bytes(Span<const u8> data) noexcept {
	u32 h = 2166136261u ^ static_cast<u32>(data.size());
	for (const u8 b : data) {
		h = static_cast<u32>((h << 5u) + h + b);
		h ^= h >> 13u;
	}
	return hash_u32(h);
}

/// Hash de texto (`StringView`) sin copiar: recorre los bytes de la vista.
[[nodiscard]] constexpr u32 hash_string(StringView text) noexcept {
	return hash_bytes(Span<const u8> {reinterpret_cast<const u8*>(text.data()), text.size()});
}

/// Functor de hash por tipo, punto de extensión de `HashMap`/`HashSet`. Por defecto
/// delega en `hash_value` (integrales, enums y punteros). Especializa `Hash<T>` para
/// tus tipos (agregados, claves compuestas).
template <class T>
struct Hash {
	[[nodiscard]] constexpr u32 operator()(const T& value) const noexcept {
		return hash_value(value);
	}
};

/// `StringView` se hashea por su contenido, no por su dirección.
template <>
struct Hash<StringView> {
	[[nodiscard]] constexpr u32 operator()(StringView text) const noexcept {
		return hash_string(text);
	}
};

} // namespace eng::util
