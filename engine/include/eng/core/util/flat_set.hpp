#pragma once

/// \file flat_set.hpp
/// `eng::util::FlatSet<T, N>`: conjunto de capacidad fija con los elementos
/// **ordenados** en un array contiguo (la versión `set` de `FlatMap`).
///
/// Mismas ventajas que `FlatMap`: sin hash, memoria exacta, iteración en orden y
/// determinista, `contains` en `O(log N)`. Ideal para pertenencia con cardinalidad
/// pequeña (ids de recurso, tags activos, teclas pulsadas sin usar `BitSet`).
///
/// Uso:
///   eng::util::FlatSet<eng::u16, 16> dirty_tiles;
///   dirty_tiles.insert(7u);
///   if (dirty_tiles.contains(7u)) { ... }

#include <eng/core/types.hpp>

namespace eng::util {

template <class T, usize N>
class FlatSet {
	static_assert(N > 0u, "FlatSet: N debe ser mayor que 0");

public:
	using value_type = T;
	using iterator = T*;
	using const_iterator = const T*;

	[[nodiscard]] static constexpr usize capacity() noexcept { return N; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size == N; }
	constexpr void clear() noexcept { m_size = 0u; }

	[[nodiscard]] constexpr T& at_index(usize index) noexcept { return m_items[index]; }
	[[nodiscard]] constexpr const T& at_index(usize index) const noexcept {
		return m_items[index];
	}
	[[nodiscard]] constexpr T* begin() noexcept { return m_items; }
	[[nodiscard]] constexpr T* end() noexcept { return m_items + m_size; }
	[[nodiscard]] constexpr const T* begin() const noexcept { return m_items; }
	[[nodiscard]] constexpr const T* end() const noexcept { return m_items + m_size; }

	/// Menor índice con `value <= m_items[index]`.
	[[nodiscard]] constexpr usize lower_bound_index(const T& value) const noexcept {
		usize lo = 0u;
		usize hi = m_size;
		while (lo < hi) {
			const usize mid = lo + (hi - lo) / 2u;
			if (m_items[mid] < value) {
				lo = mid + 1u;
			} else {
				hi = mid;
			}
		}
		return lo;
	}

	[[nodiscard]] constexpr bool contains(const T& value) const noexcept {
		const usize i = lower_bound_index(value);
		return i < m_size && equal(m_items[i], value);
	}

	/// Inserta si no estaba. `false` si era duplicado o el conjunto está lleno.
	constexpr bool insert(const T& value) noexcept {
		const usize i = lower_bound_index(value);
		if (i < m_size && equal(m_items[i], value)) {
			return false; // duplicado
		}
		if (m_size == N) {
			return false; // lleno
		}
		for (usize j = m_size; j > i; --j) {
			m_items[j] = m_items[j - 1u];
		}
		m_items[i] = value;
		++m_size;
		return true;
	}

	/// Borra si estaba. `false` si no estaba.
	constexpr bool erase(const T& value) noexcept {
		const usize i = lower_bound_index(value);
		if (i >= m_size || !equal(m_items[i], value)) {
			return false;
		}
		for (usize j = i; j + 1u < m_size; ++j) {
			m_items[j] = m_items[j + 1u];
		}
		--m_size;
		return true;
	}

private:
	[[nodiscard]] static constexpr bool equal(const T& a, const T& b) noexcept {
		return !(a < b) && !(b < a);
	}

	T m_items[N] {};
	usize m_size = 0u;
};

} // namespace eng::util
