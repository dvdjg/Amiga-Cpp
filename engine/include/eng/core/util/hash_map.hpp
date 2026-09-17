#pragma once

/// \file hash_map.hpp
/// `eng::util::HashMap<K, V, N>`: tabla hash de capacidad fija con **direccionamiento
/// abierto** y **sondeo lineal** (open addressing), sin heap.
///
/// Cuándo usarlo: cuando `FlatMap` no basta porque la cardinalidad crece y el coste
/// `O(N)` de insertar deja de ser aceptable (internado de cadenas, conjuntos de ids,
/// deduplicación en carga). La búsqueda es `O(1)` promedio.
///
/// Diseño pensado para el 68000:
/// - capacidad interna **potencia de dos** → índice por máscara `& (slots-1)`, sin
///   división ni módulo (`__udivsi3`);
/// - factor de carga máximo 3/4 → siempre hay hueco libre y el sondeo termina;
/// - borrado por **desplazamiento hacia atrás** (back-shift) en vez de lápidas: no
///   acumula basura y `size()` sigue siendo exacto;
/// - el hash (`hash.hpp`) no usa multiplicación de 32×32, así que no hay `__mulsi3`.
///
/// La ocupación se lleva con `BitSet<slots>`, de modo que las claves no necesitan un
/// valor centinela. `K` y `V` deben ser construibles por defecto.
///
/// Uso:
///   eng::util::HashMap<eng::util::StringView, eng::u16, 64> names;
///   names.insert(eu::StringView("hero"), 3u);

#include <eng/core/types.hpp>
#include <eng/core/util/bitset.hpp>
#include <eng/core/util/hash.hpp>

namespace eng::util {

namespace detail {

/// Número de ranuras (potencia de dos, ≥ 8) para alojar `n` entradas con carga ≤ 3/4.
[[nodiscard]] constexpr usize hash_slots_for(usize n) noexcept {
	usize wanted = (n * 4u + 2u) / 3u;
	if (wanted < 8u) {
		wanted = 8u;
	}
	usize slots = 8u;
	while (slots < wanted) {
		slots <<= 1u;
	}
	return slots;
}

} // namespace detail

template <class K, class V, usize N>
class HashMap {
	static_assert(N > 0u, "HashMap: N debe ser mayor que 0");

public:
	static constexpr usize capacity() noexcept { return N; }
	static constexpr usize slot_count() noexcept { return detail::hash_slots_for(N); }

	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	/// Lleno cuando se alcanza el número de entradas declarado (`N`); las ranuras
	/// sobrantes (≥ 1/4) mantienen siempre el sondeo acotado.
	[[nodiscard]] constexpr bool full() const noexcept { return m_size >= N; }

	constexpr void clear() noexcept {
		m_used.reset();
		m_size = 0u;
	}

	[[nodiscard]] constexpr V* find(const K& key) noexcept {
		if (m_size == 0u) {
			return nullptr;
		}
		const usize mask = slots() - 1u;
		for (usize i = m_hash(key) & mask; m_used.test(i); i = (i + 1u) & mask) {
			if (m_keys[i] == key) {
				return &m_values[i];
			}
		}
		return nullptr;
	}
	[[nodiscard]] constexpr const V* find(const K& key) const noexcept {
		if (m_size == 0u) {
			return nullptr;
		}
		const usize mask = slots() - 1u;
		for (usize i = m_hash(key) & mask; m_used.test(i); i = (i + 1u) & mask) {
			if (m_keys[i] == key) {
				return &m_values[i];
			}
		}
		return nullptr;
	}
	[[nodiscard]] constexpr bool contains(const K& key) const noexcept {
		return find(key) != nullptr;
	}

	/// Inserta una clave nueva. Devuelve el valor o `nullptr` si ya existe o si la
	/// tabla está llena (no sobrescribe).
	constexpr V* insert(const K& key, const V& value) noexcept {
		if (full()) {
			return nullptr;
		}
		const usize mask = slots() - 1u;
		usize i = m_hash(key) & mask;
		for (;;) {
			if (!m_used.test(i)) {
				m_keys[i] = key;
				m_values[i] = value;
				m_used.set(i);
				++m_size;
				return &m_values[i];
			}
			if (m_keys[i] == key) {
				return nullptr; // duplicado
			}
			i = (i + 1u) & mask;
		}
	}

	/// Inserta o actualiza. Devuelve `true` si la clave era nueva.
	constexpr bool insert_or_assign(const K& key, const V& value) noexcept {
		const usize mask = slots() - 1u;
		for (usize i = m_hash(key) & mask; m_used.test(i); i = (i + 1u) & mask) {
			if (m_keys[i] == key) {
				m_values[i] = value;
				return false;
			}
		}
		if (full()) {
			return false;
		}
		usize i = m_hash(key) & mask;
		while (m_used.test(i)) {
			i = (i + 1u) & mask;
		}
		m_keys[i] = key;
		m_values[i] = value;
		m_used.set(i);
		++m_size;
		return true;
	}

	/// Borra la clave. `false` si no estaba. Reubica el clúster (back-shift).
	constexpr bool erase(const K& key) noexcept {
		if (m_size == 0u) {
			return false;
		}
		const usize mask = slots() - 1u;
		usize i = m_hash(key) & mask;
		while (m_used.test(i)) {
			if (m_keys[i] == key) {
				back_shift_erase(i);
				return true;
			}
			i = (i + 1u) & mask;
		}
		return false;
	}

	/// Recorre las entradas vivas (orden por ranura, no por clave).
	template <class Fn>
	constexpr void for_each(Fn fn) const {
		for (usize i = 0; i < slots(); ++i) {
			if (m_used.test(i)) {
				fn(m_keys[i], m_values[i]);
			}
		}
	}

	/// ¿Está la ranura `i` ocupada? (introspección de test/diagnóstico)
	[[nodiscard]] constexpr bool slot_used(usize i) const noexcept { return m_used.test(i); }
	[[nodiscard]] constexpr const K& slot_key(usize i) const noexcept { return m_keys[i]; }
	[[nodiscard]] constexpr const V& slot_value(usize i) const noexcept { return m_values[i]; }

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
			if (d_hole <= d_j) { // j puede retroceder al hueco sin salir de su camino
				m_keys[hole] = m_keys[j];
				m_values[hole] = m_values[j];
				m_used.set(hole);
				m_used.reset(j);
				hole = j;
			}
		}
	}

	K m_keys[slot_count()] {};
	V m_values[slot_count()] {};
	BitSet<slot_count()> m_used {};
	Hash<K> m_hash {};
	usize m_size = 0u;
};

} // namespace eng::util
