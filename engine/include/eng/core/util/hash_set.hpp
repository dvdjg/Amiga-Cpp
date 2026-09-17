#pragma once

/// \file hash_set.hpp
/// `eng::util::HashSet<T, N>`: conjunto de capacidad fija con tabla hash de
/// direccionamiento abierto y sondeo lineal (la versión `set` de `HashMap`).
///
/// Para pertenencia con cardinalidad que no cabe cómodamente en un array ordenado
/// (`FlatSet`) o cuando las claves no tienen orden natural. Mismo diseño que
/// `HashMap`: potencia de dos, carga ≤ 3/4, borrado por back-shift y hash sin
/// `__mulsi3`. `T` debe ser construible por defecto.
///
/// Uso:
///   eng::util::HashSet<eng::u16, 128> visited;
///   visited.insert(node_id);

#include <eng/core/types.hpp>
#include <eng/core/util/bitset.hpp>
#include <eng/core/util/hash.hpp>
#include <eng/core/util/hash_map.hpp>

namespace eng::util {

template <class T, usize N>
class HashSet {
	static_assert(N > 0u, "HashSet: N debe ser mayor que 0");

public:
	static constexpr usize capacity() noexcept { return N; }
	static constexpr usize slot_count() noexcept { return detail::hash_slots_for(N); }

	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size >= N; }

	constexpr void clear() noexcept {
		m_used.reset();
		m_size = 0u;
	}

	[[nodiscard]] constexpr bool contains(const T& value) const noexcept {
		if (m_size == 0u) {
			return false;
		}
		const usize mask = slots() - 1u;
		for (usize i = m_hash(value) & mask; m_used.test(i); i = (i + 1u) & mask) {
			if (m_keys[i] == value) {
				return true;
			}
		}
		return false;
	}

	/// Inserta si no estaba. `false` si era duplicado o la tabla está llena.
	constexpr bool insert(const T& value) noexcept {
		if (full()) {
			return false;
		}
		const usize mask = slots() - 1u;
		usize i = m_hash(value) & mask;
		for (;;) {
			if (!m_used.test(i)) {
				m_keys[i] = value;
				m_used.set(i);
				++m_size;
				return true;
			}
			if (m_keys[i] == value) {
				return false; // duplicado
			}
			i = (i + 1u) & mask;
		}
	}

	/// Borra si estaba. `false` si no estaba. Reubica el clúster (back-shift).
	constexpr bool erase(const T& value) noexcept {
		if (m_size == 0u) {
			return false;
		}
		const usize mask = slots() - 1u;
		usize i = m_hash(value) & mask;
		while (m_used.test(i)) {
			if (m_keys[i] == value) {
				back_shift_erase(i);
				return true;
			}
			i = (i + 1u) & mask;
		}
		return false;
	}

	template <class Fn>
	constexpr void for_each(Fn fn) const {
		for (usize i = 0; i < slots(); ++i) {
			if (m_used.test(i)) {
				fn(m_keys[i]);
			}
		}
	}

	[[nodiscard]] constexpr bool slot_used(usize i) const noexcept { return m_used.test(i); }
	[[nodiscard]] constexpr const T& slot_key(usize i) const noexcept { return m_keys[i]; }

private:
	[[nodiscard]] static constexpr usize slots() noexcept { return slot_count(); }

	constexpr void back_shift_erase(usize hole) noexcept {
		const usize mask = slots() - 1u;
		m_used.reset(hole);
		--m_size;
		for (usize j = (hole + 1u) & mask; m_used.test(j); j = (j + 1u) & mask) {
			const usize ideal = m_hash(m_keys[j]) & mask;
			const usize d_hole = (hole - ideal) & mask;
			const usize d_j = (j - ideal) & mask;
			if (d_hole <= d_j) {
				m_keys[hole] = m_keys[j];
				m_used.set(hole);
				m_used.reset(j);
				hole = j;
			}
		}
	}

	T m_keys[slot_count()] {};
	BitSet<slot_count()> m_used {};
	Hash<T> m_hash {};
	usize m_size = 0u;
};

} // namespace eng::util
