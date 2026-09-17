#pragma once

/// \file enum_set.hpp
/// `eng::util::EnumSet<E, N>`: conjunto de bits **tipado por un `enum`**.
///
/// Escribe `set(Flag::Jump)` en vez de un índice a mano y evita el error clásico de
/// pasar el valor equivocado: solo acepta valores de `E`. Por dentro es un `BitSet<N>`:
/// el índice es el **valor entero** del enumerado, así que sus valores deben caer en
/// `[0, N)` (documentado; fuera de rango dispara `illegal`, como `BitSet`).
///
/// Uso:
///   enum class Key : eng::u8 { Left = 0, Right = 1, Fire = 2 };
///   eng::util::EnumSet<Key, 3> pressed;
///   pressed.set(Key::Fire);
///   if (pressed.test(Key::Fire)) { ... }

#include <eng/core/types.hpp>
#include <eng/core/util/bitset.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

template <class E, usize N>
class EnumSet {
	static_assert(is_enum_v<E>, "EnumSet: E debe ser un enum");
	static_assert(N > 0u, "EnumSet: N debe ser mayor que 0");

public:
	using enum_type = E;

	[[nodiscard]] static constexpr usize capacity() noexcept { return N; }

	constexpr void set(E value) noexcept { m_bits.set(index(value)); }
	constexpr void set(E value, bool on) noexcept { on ? m_bits.set(index(value)) : m_bits.reset(index(value)); }
	constexpr void reset(E value) noexcept { m_bits.reset(index(value)); }
	constexpr void reset() noexcept { m_bits.reset(); }
	constexpr void flip(E value) noexcept { m_bits.flip(index(value)); }
	constexpr void flip() noexcept { m_bits.flip(); }
	[[nodiscard]] constexpr bool test(E value) const noexcept { return m_bits.test(index(value)); }

	[[nodiscard]] constexpr usize count() const noexcept { return m_bits.count(); }
	[[nodiscard]] constexpr bool any() const noexcept { return m_bits.any(); }
	[[nodiscard]] constexpr bool none() const noexcept { return m_bits.none(); }
	[[nodiscard]] constexpr bool all() const noexcept { return m_bits.all(); }

	[[nodiscard]] constexpr const BitSet<N>& bits() const noexcept { return m_bits; }
	[[nodiscard]] constexpr BitSet<N>& bits() noexcept { return m_bits; }

	[[nodiscard]] friend constexpr bool operator==(const EnumSet& a, const EnumSet& b) noexcept {
		return a.m_bits == b.m_bits;
	}

private:
	[[nodiscard]] static constexpr usize index(E value) noexcept {
		return static_cast<usize>(static_cast<underlying_type_t<E>>(value));
	}

	BitSet<N> m_bits {};
};

} // namespace eng::util
