#pragma once

/// \file dynamic_hash_map.hpp
/// `eng::util::DynamicHashMap<K, V, A>`: tabla hash que **crece** reservando sus
/// tablas en un `Allocator` (arena/bump), para la fase `init`/carga.
///
/// Es la versión dinámica de `HashMap`: cuando el número de entradas se conoce solo en
/// tiempo de ejecución (internado de cadenas del loader, deduplicación de recursos),
/// esta tabla reserva las tablas de claves/valores/ocupación y las **rehace** (rehash)
/// al cruzar el 3/4 de carga. Sigue sin `malloc`: el allocator es una arena, y crecer
/// desperdicia la tabla vieja (bump), un coste aceptable en `init`.
///
/// Diseño para 68000: capacidad **potencia de dos** (índice por máscara, sin
/// `__udivsi3`), sondeo lineal, borrado por back-shift y hash sin multiplicación de
/// 32×32 (`hash.hpp`). Claves y valores deben ser **copiables trivialmente**: el
/// almacenamiento es crudo (no se construyen objetos).
///
/// Uso:
///   eng::util::DynamicHashMap<eng::util::StringView, eng::u16, eng::util::ArenaAlloc>
///       names {alloc};
///   names.insert_or_assign(name, id);   // crece solo si hace falta

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/allocator.hpp>
#include <eng/core/util/hash.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

template <class K, class V, class A = NullAlloc>
class DynamicHashMap {
	static_assert(is_trivially_copyable_v<K>, "DynamicHashMap: K debe ser copiable trivialmente");
	static_assert(is_trivially_copyable_v<V>, "DynamicHashMap: V debe ser copiable trivialmente");

public:
	using key_type = K;
	using mapped_type = V;

	constexpr DynamicHashMap() noexcept = default;
	explicit constexpr DynamicHashMap(A alloc) noexcept : m_alloc(alloc) {}

	DynamicHashMap(const DynamicHashMap&) = delete;
	DynamicHashMap& operator=(const DynamicHashMap&) = delete;

	constexpr DynamicHashMap(DynamicHashMap&& other) noexcept
		: m_alloc(other.m_alloc), m_keys(other.m_keys), m_values(other.m_values),
		  m_used(other.m_used), m_slots(other.m_slots), m_size(other.m_size) {
		other.m_keys = nullptr;
		other.m_values = nullptr;
		other.m_used = nullptr;
		other.m_slots = 0u;
		other.m_size = 0u;
	}
	constexpr DynamicHashMap& operator=(DynamicHashMap&& other) noexcept {
		if (this != &other) {
			release();
			m_alloc = other.m_alloc;
			m_keys = other.m_keys;
			m_values = other.m_values;
			m_used = other.m_used;
			m_slots = other.m_slots;
			m_size = other.m_size;
			other.m_keys = nullptr;
			other.m_values = nullptr;
			other.m_used = nullptr;
			other.m_slots = 0u;
			other.m_size = 0u;
		}
		return *this;
	}
	~DynamicHashMap() { release(); }

	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr usize slots() const noexcept { return m_slots; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }

	/// Garantiza capacidad para `entries` entradas (rehace si hace falta).
	constexpr bool reserve(usize entries) noexcept { return grow(entries); }

	/// Vacía conservando las tablas reservadas.
	constexpr void clear() noexcept {
		if (m_used != nullptr) {
			for (usize w = 0; w < word_count(); ++w) {
				m_used[w] = 0u;
			}
		}
		m_size = 0u;
	}

	[[nodiscard]] constexpr V* find(const K& key) noexcept {
		if (m_slots == 0u) {
			return nullptr;
		}
		const usize mask = m_slots - 1u;
		for (usize i = m_hash(key) & mask; used(i); i = (i + 1u) & mask) {
			if (m_keys[i] == key) {
				return &m_values[i];
			}
		}
		return nullptr;
	}
	[[nodiscard]] constexpr const V* find(const K& key) const noexcept {
		if (m_slots == 0u) {
			return nullptr;
		}
		const usize mask = m_slots - 1u;
		for (usize i = m_hash(key) & mask; used(i); i = (i + 1u) & mask) {
			if (m_keys[i] == key) {
				return &m_values[i];
			}
		}
		return nullptr;
	}
	[[nodiscard]] constexpr bool contains(const K& key) const noexcept {
		return find(key) != nullptr;
	}

	/// Inserta una clave nueva (rehace si la carga supera 3/4). `nullptr` si la clave
	/// ya existe o si el allocator no puede.
	constexpr V* insert(const K& key, const V& value) noexcept {
		if (m_slots == 0u && !grow(1u)) {
			return nullptr;
		}
		if (!probe_free(key, value)) {
			return nullptr;
		}
		return &m_values[probe_slot(key)];
	}

	/// Inserta o actualiza. `true` si la clave era nueva.
	constexpr bool insert_or_assign(const K& key, const V& value) noexcept {
		V* existing = find(key);
		if (existing != nullptr) {
			*existing = value;
			return false;
		}
		return insert(key, value) != nullptr;
	}

	/// Borra la clave. `false` si no estaba. Reubica el clúster (back-shift).
	constexpr bool erase(const K& key) noexcept {
		if (m_slots == 0u) {
			return false;
		}
		const usize mask = m_slots - 1u;
		usize i = m_hash(key) & mask;
		while (used(i)) {
			if (m_keys[i] == key) {
				back_shift_erase(i);
				return true;
			}
			i = (i + 1u) & mask;
		}
		return false;
	}

	template <class Fn>
	constexpr void for_each(Fn fn) const {
		for (usize i = 0; i < m_slots; ++i) {
			if (used(i)) {
				fn(m_keys[i], m_values[i]);
			}
		}
	}

private:
	using word_t = __UINT32_TYPE__;
	static constexpr usize kWordBits = 32u;

	[[nodiscard]] constexpr usize word_count() const noexcept { return m_slots / kWordBits; }
	[[nodiscard]] constexpr bool used(usize i) const noexcept {
		return (m_used[i / kWordBits] & (static_cast<word_t>(1) << (i % kWordBits))) != 0u;
	}
	constexpr void set_used(usize i) noexcept {
		m_used[i / kWordBits] =
			static_cast<word_t>(m_used[i / kWordBits] |
					    (static_cast<word_t>(1) << (i % kWordBits)));
	}
	constexpr void clear_used(usize i) noexcept {
		m_used[i / kWordBits] = static_cast<word_t>(
			m_used[i / kWordBits] & static_cast<word_t>(~(static_cast<word_t>(1) << (i % kWordBits))));
	}

	/// Primera ranura ocupada por `key` o libre (sin rehasear).
	[[nodiscard]] constexpr usize probe_slot(const K& key) const noexcept {
		const usize mask = m_slots - 1u;
		usize i = m_hash(key) & mask;
		while (used(i) && !(m_keys[i] == key)) {
			i = (i + 1u) & mask;
		}
		return i;
	}

	/// Escribe una entrada nueva; rehace si la clave no cabría con la carga objetivo.
	constexpr bool probe_free(const K& key, const V& value) noexcept {
		const usize mask = m_slots - 1u;
		usize i = m_hash(key) & mask;
		while (used(i)) {
			if (m_keys[i] == key) {
				return false; // duplicado
			}
			i = (i + 1u) & mask;
		}
		if (m_size + 1u > (m_slots * 3u) / 4u) {
			if (!grow(m_size + 1u)) {
				return false;
			}
			return write_new(key, value);
		}
		m_keys[i] = key;
		m_values[i] = value;
		set_used(i);
		++m_size;
		return true;
	}

	/// Escritura directa (sin comprobar duplicado ni carga): usa `probe_slot`.
	constexpr bool write_new(const K& key, const V& value) noexcept {
		const usize slot = probe_slot(key);
		if (used(slot)) {
			return false; // no debería ocurrir tras rehash
		}
		m_keys[slot] = key;
		m_values[slot] = value;
		set_used(slot);
		++m_size;
		return true;
	}

	/// Reasigna las tablas a `min_entries` (redondeando a potencia de dos) y rehashea.
	constexpr bool grow(usize min_entries) noexcept {
		usize wanted = min_entries + min_entries / 2u + 1u;
		// Mínimo una palabra de ocupación (32 ranuras): `used()` indexa por palabra.
		usize new_slots = kWordBits;
		while (new_slots < wanted) {
			new_slots <<= 1u;
		}
		if (new_slots == m_slots) {
			return true;
		}
		const Span<u8> kb = m_alloc.allocate(new_slots * sizeof(K), alignof(K));
		const Span<u8> vb = m_alloc.allocate(new_slots * sizeof(V), alignof(V));
		const Span<u8> ub = m_alloc.allocate((new_slots / kWordBits) * sizeof(word_t),
						     alignof(word_t));
		if (kb.empty() || vb.empty() || ub.empty()) {
			return false;
		}
		word_t* new_used = reinterpret_cast<word_t*>(ub.data());
		for (usize w = 0; w < new_slots / kWordBits; ++w) {
			new_used[w] = 0u;
		}
		// Conserva las tablas viejas para reinsertar y guárdalas para liberar.
		K* old_keys = m_keys;
		V* old_values = m_values;
		word_t* old_used = m_used;
		const usize old_slots = m_slots;
		m_keys = reinterpret_cast<K*>(kb.data());
		m_values = reinterpret_cast<V*>(vb.data());
		m_used = new_used;
		m_slots = new_slots;
		m_size = 0u;
		if (old_keys != nullptr) {
			const usize old_words = old_slots / kWordBits;
			for (usize i = 0; i < old_slots; ++i) {
				if ((old_used[i / kWordBits] &
				     (static_cast<word_t>(1) << (i % kWordBits))) != 0u) {
					write_new(old_keys[i], old_values[i]);
				}
			}
			m_alloc.deallocate(
				Span<u8> {reinterpret_cast<u8*>(old_keys), old_slots * sizeof(K)});
			m_alloc.deallocate(
				Span<u8> {reinterpret_cast<u8*>(old_values), old_slots * sizeof(V)});
			m_alloc.deallocate(
				Span<u8> {reinterpret_cast<u8*>(old_used), old_words * sizeof(word_t)});
		}
		return true;
	}

	constexpr void back_shift_erase(usize hole) noexcept {
		const usize mask = m_slots - 1u;
		clear_used(hole);
		--m_size;
		for (usize j = (hole + 1u) & mask; used(j); j = (j + 1u) & mask) {
			const usize ideal = m_hash(m_keys[j]) & mask;
			const usize d_hole = (hole - ideal) & mask;
			const usize d_j = (j - ideal) & mask;
			if (d_hole <= d_j) {
				m_keys[hole] = m_keys[j];
				m_values[hole] = m_values[j];
				set_used(hole);
				clear_used(j);
				hole = j;
			}
		}
	}

	constexpr void release() noexcept {
		if (m_keys != nullptr) {
			m_alloc.deallocate(Span<u8> {reinterpret_cast<u8*>(m_keys), m_slots * sizeof(K)});
		}
		if (m_values != nullptr) {
			m_alloc.deallocate(
				Span<u8> {reinterpret_cast<u8*>(m_values), m_slots * sizeof(V)});
		}
		if (m_used != nullptr) {
			m_alloc.deallocate(
				Span<u8> {reinterpret_cast<u8*>(m_used), word_count() * sizeof(word_t)});
		}
		m_keys = nullptr;
		m_values = nullptr;
		m_used = nullptr;
		m_slots = 0u;
		m_size = 0u;
	}

	A m_alloc {};
	K* m_keys = nullptr;
	V* m_values = nullptr;
	word_t* m_used = nullptr;
	usize m_slots = 0u;
	usize m_size = 0u;

	Hash<K> m_hash {};
};

} // namespace eng::util
