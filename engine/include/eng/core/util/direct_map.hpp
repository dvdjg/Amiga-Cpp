#pragma once

/// \file direct_map.hpp
/// `eng::util::DirectMap<V, N>`: mapa de **clave densa** `0..N-1` con acceso `O(1)`
/// directo, sin hash ni comparaciones.
///
/// Cuando las claves son un subconjunto pequeño y contiguo de enteros —índices de
/// tile, ids de canal de sprite, índices de plano— no hace falta ni ordenar ni
/// hashear: un array `V[N]` indexado por la propia clave más un `BitSet<N>` de
/// presencia da el acceso más barato posible (una carga indexada y un test de bit).
/// Es el caso más frecuente en un motor de tiles.
///
/// No asigna memoria. `V` debe ser copiable trivialmente (el almacenamiento no
/// construye objetos) y las claves fuera de `[0, N)` se rechazan sin abortar.
///
/// Uso:
///   eng::util::DirectMap<TileDef, 256> defs;   // key = índice de tile
///   defs.insert(3u, def);
///   const TileDef* d = defs.find(3u);

#include <eng/core/types.hpp>
#include <eng/core/util/bitset.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

template <class V, usize N>
class DirectMap {
	static_assert(N > 0u, "DirectMap: N debe ser mayor que 0");
	static_assert(is_trivially_copyable_v<V>, "DirectMap: V debe ser copiable trivialmente");

public:
	static constexpr usize key_range() noexcept { return N; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size == N; }

	constexpr void clear() noexcept {
		m_present.reset();
		m_size = 0u;
	}

	[[nodiscard]] constexpr bool contains(usize key) const noexcept {
		return key < N && m_present.test(key);
	}

	[[nodiscard]] constexpr V* find(usize key) noexcept {
		return contains(key) ? &slot(key) : nullptr;
	}
	[[nodiscard]] constexpr const V* find(usize key) const noexcept {
		return contains(key) ? &slot(key) : nullptr;
	}

	/// Valor de la clave; detiene la CPU (`illegal`) si la clave no está.
	[[nodiscard]] constexpr V& at(usize key) noexcept {
		if (!contains(key)) {
			eng::detail::span_out_of_bounds();
		}
		return slot(key);
	}
	[[nodiscard]] constexpr const V& at(usize key) const noexcept {
		if (!contains(key)) {
			eng::detail::span_out_of_bounds();
		}
		return slot(key);
	}

	/// Inserta una clave libre. Devuelve `nullptr` si está ocupada o fuera de rango.
	constexpr V* insert(usize key, const V& value) noexcept {
		if (key >= N || m_present.test(key)) {
			return nullptr;
		}
		slot(key) = value;
		m_present.set(key);
		++m_size;
		return &slot(key);
	}

	/// Inserta o actualiza. Devuelve `true` si la clave era nueva; `false` si estaba
	/// (actualizada) o si la clave está fuera de rango (no se toca nada).
	constexpr bool insert_or_assign(usize key, const V& value) noexcept {
		if (key >= N) {
			return false;
		}
		const bool fresh = !m_present.test(key);
		slot(key) = value;
		if (fresh) {
			m_present.set(key);
			++m_size;
		}
		return fresh;
	}

	/// Borra la clave. `false` si no estaba.
	constexpr bool erase(usize key) noexcept {
		if (!contains(key)) {
			return false;
		}
		m_present.reset(key);
		--m_size;
		return true;
	}

	/// Recorre las claves presentes en orden creciente.
	template <class Fn>
	constexpr void for_each(Fn fn) const {
		for (usize key = 0; key < N; ++key) {
			if (m_present.test(key)) {
				fn(key, slot(key));
			}
		}
	}

private:
	[[nodiscard]] constexpr V& slot(usize key) noexcept {
		return *reinterpret_cast<V*>(m_storage + key * sizeof(V));
	}
	[[nodiscard]] constexpr const V& slot(usize key) const noexcept {
		return *reinterpret_cast<const V*>(m_storage + key * sizeof(V));
	}

	alignas(V) u8 m_storage[N * sizeof(V)] {};
	BitSet<N> m_present {};
	usize m_size = 0u;
};

} // namespace eng::util
