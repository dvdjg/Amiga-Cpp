#pragma once

/// \file flat_map.hpp
/// `eng::util::FlatMap<K, V, N>`: mapa de capacidad fija con las entradas **ordenadas
/// por clave** en un array contiguo (estilo `flat_map`).
///
/// Para el rango de tamaños del engine (props de entidad, tile→def, tablas de
/// configuración) gana a una tabla hash: no hay función hash, la memoria es exacta
/// (`N·(K+V)`), la iteración es contigua y en orden de clave (determinista), y la
/// búsqueda es una búsqueda binaria (`O(log N)`) sobre un array que cabe en línea de
/// caché —o, en 68000 sin caché, con muy pocas lecturas—. El precio es que
/// insertar/borrar es `O(N)` por el desplazamiento, aceptable con `N` pequeño.
///
/// No asigna memoria. `insert` devuelve `nullptr` si la clave ya existe o si el mapa
/// está lleno (no sobrescribe ni aborta); para ese caso está `insert_or_assign`.
///
/// Uso:
///   eng::util::FlatMap<eng::u16, eng::u8, 32> tile_flags;
///   tile_flags.insert(3u, 0x12u);
///   const eng::u8* f = tile_flags.find(3u);

#include <eng/core/types.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

template <class K, class V, usize N>
class FlatMap {
	static_assert(N > 0u, "FlatMap: N debe ser mayor que 0");

public:
	struct Entry {
		K key {};
		V value {};
	};

	using value_type = Entry;
	using iterator = Entry*;
	using const_iterator = const Entry*;

	[[nodiscard]] static constexpr usize capacity() noexcept { return N; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size == N; }
	constexpr void clear() noexcept { m_size = 0u; }

	/// Acceso por orden de clave (0 = menor).
	[[nodiscard]] constexpr Entry& at_index(usize index) noexcept { return m_entries[index]; }
	[[nodiscard]] constexpr const Entry& at_index(usize index) const noexcept {
		return m_entries[index];
	}
	[[nodiscard]] constexpr Entry* begin() noexcept { return m_entries; }
	[[nodiscard]] constexpr Entry* end() noexcept { return m_entries + m_size; }
	[[nodiscard]] constexpr const Entry* begin() const noexcept { return m_entries; }
	[[nodiscard]] constexpr const Entry* end() const noexcept { return m_entries + m_size; }

	/// Menor índice con `key <= m_entries[index].key` (igual que `lower_bound`).
	[[nodiscard]] constexpr usize lower_bound_index(const K& key) const noexcept {
		usize lo = 0u;
		usize hi = m_size;
		while (lo < hi) {
			const usize mid = lo + (hi - lo) / 2u;
			if (m_entries[mid].key < key) {
				lo = mid + 1u;
			} else {
				hi = mid;
			}
		}
		return lo;
	}

	[[nodiscard]] constexpr V* find(const K& key) noexcept {
		const usize i = lower_bound_index(key);
		if (i < m_size && keys_equal(m_entries[i].key, key)) {
			return &m_entries[i].value;
		}
		return nullptr;
	}
	[[nodiscard]] constexpr const V* find(const K& key) const noexcept {
		const usize i = lower_bound_index(key);
		if (i < m_size && keys_equal(m_entries[i].key, key)) {
			return &m_entries[i].value;
		}
		return nullptr;
	}
	[[nodiscard]] constexpr bool contains(const K& key) const noexcept {
		return find(key) != nullptr;
	}

	/// Inserta una clave nueva. Devuelve el valor o `nullptr` si ya existe o si el
	/// mapa está lleno (no sobrescribe).
	constexpr V* insert(const K& key, const V& value) noexcept {
		if (m_size == N) {
			return nullptr;
		}
		const usize i = lower_bound_index(key);
		if (i < m_size && keys_equal(m_entries[i].key, key)) {
			return nullptr;
		}
		shift_right_from(i);
		m_entries[i] = Entry {key, value};
		++m_size;
		return &m_entries[i].value;
	}

	/// Inserta o actualiza. Devuelve `true` si la clave era nueva.
	constexpr bool insert_or_assign(const K& key, const V& value) noexcept {
		const usize i = lower_bound_index(key);
		if (i < m_size && keys_equal(m_entries[i].key, key)) {
			m_entries[i].value = value;
			return false;
		}
		if (m_size == N) {
			return false; // lleno y la clave no existe
		}
		shift_right_from(i);
		m_entries[i] = Entry {key, value};
		++m_size;
		return true;
	}

	/// Borra la clave. `false` si no estaba.
	constexpr bool erase(const K& key) noexcept {
		const usize i = lower_bound_index(key);
		if (i >= m_size || !keys_equal(m_entries[i].key, key)) {
			return false;
		}
		for (usize j = i; j + 1u < m_size; ++j) {
			m_entries[j] = m_entries[j + 1u];
		}
		--m_size;
		return true;
	}

private:
	/// Igualdad sin exigir `operator==`: dos claves son iguales si ninguna es menor.
	[[nodiscard]] static constexpr bool keys_equal(const K& a, const K& b) noexcept {
		return !(a < b) && !(b < a);
	}

	constexpr void shift_right_from(usize index) noexcept {
		for (usize j = m_size; j > index; --j) {
			m_entries[j] = m_entries[j - 1u];
		}
	}

	Entry m_entries[N] {};
	usize m_size = 0u;
};

} // namespace eng::util
