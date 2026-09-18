#pragma once

/// \file lru_cache.hpp
/// `eng::util::LruCache<K, V, N>`: **caché LRU de capacidad fija** (sin heap) con
/// `get`/`put`/`erase` en `O(1)`. Generaliza el patrón de `eng::field::ChunkCache`
/// (tiles, sprites, mapas): índice hash `clave -> ranura` + lista doblemente enlazada
/// intrusiva sobre las ranuras para la recencia. Al insertar con la caché llena se
/// **desaloja la entrada menos usada recientemente** (la cola).
///
/// `get` toca la entrada (la hace la más reciente); `peek` la consulta sin cambiar la
/// recencia. `K` necesita `Hash<K>` (los enteros ya lo tienen; un tipo propio aporta la
/// especialización). `K` y `V` deben ser construibles por defecto.
///
/// Uso:
///   eng::util::LruCache<eng::u16, Tile, 32> cache;
///   if (Tile* t = cache.get(id)) { ... }   // hit (y marca reciente)
///   cache.put(id, tile);                     // evicta la LRU si está llena
///
/// Verificación: HOST-127.

#include <eng/core/types.hpp>
#include <eng/core/util/hash_map.hpp>

namespace eng::util {

template <class K, class V, eng::u16 N>
class LruCache {
	static_assert(N > 0u, "LruCache: N debe ser mayor que 0");

	static constexpr eng::u16 no_slot = 0xffffu;

public:
	constexpr LruCache() noexcept { clear(); }

	[[nodiscard]] static constexpr eng::u16 capacity() noexcept { return N; }
	[[nodiscard]] constexpr eng::u16 size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size == N; }

	constexpr void clear() noexcept {
		m_index.clear();
		m_size = 0u;
		m_head = no_slot;
		m_tail = no_slot;
		m_free = 0u;
		for (eng::u16 i = 0u; i < N; ++i) {
			m_slots[i].prev = no_slot;
			m_slots[i].next = (static_cast<eng::u16>(i + 1u) < N)
						  ? static_cast<eng::u16>(i + 1u)
						  : no_slot;
		}
	}

	/// Valor de `key`, marcándolo como el más reciente. `nullptr` si no está.
	[[nodiscard]] constexpr V* get(const K& key) noexcept {
		const eng::u16* slot = m_index.find(key);
		if (slot == nullptr) {
			return nullptr;
		}
		touch(*slot);
		return &m_slots[*slot].value;
	}
	/// Como `get` pero sin tocar la recencia.
	[[nodiscard]] constexpr const V* peek(const K& key) const noexcept {
		const eng::u16* slot = m_index.find(key);
		return slot == nullptr ? nullptr : &m_slots[*slot].value;
	}
	[[nodiscard]] constexpr bool contains(const K& key) const noexcept {
		return m_index.contains(key);
	}

	/// Inserta o actualiza `key`. Devuelve `true` si la clave era nueva. Si está llena y
	/// la clave es nueva, desaloja la entrada menos reciente.
	constexpr bool put(const K& key, const V& value) noexcept {
		if (const eng::u16* existing = m_index.find(key)) {
			m_slots[*existing].value = value;
			touch(*existing);
			return false;
		}
		const eng::u16 slot = take_slot();
		m_slots[slot].key = key;
		m_slots[slot].value = value;
		push_front(slot);
		m_index.insert_or_assign(key, slot);
		++m_size;
		return true;
	}

	/// Borra `key`. `false` si no estaba.
	constexpr bool erase(const K& key) noexcept {
		const eng::u16* slot = m_index.find(key);
		if (slot == nullptr) {
			return false;
		}
		const eng::u16 s = *slot;
		m_index.erase(key);
		unlink(s);
		release_slot(s);
		--m_size;
		return true;
	}

private:
	struct Slot {
		K key {};
		V value {};
		eng::u16 prev = no_slot;
		eng::u16 next = no_slot;
	};

	/// Devuelve una ranura libre, o la de la LRU (desalojándola) si no hay.
	[[nodiscard]] constexpr eng::u16 take_slot() noexcept {
		if (m_free != no_slot) {
			const eng::u16 s = m_free;
			m_free = m_slots[s].next;
			return s;
		}
		const eng::u16 s = m_tail;
		m_index.erase(m_slots[s].key);
		unlink(s);
		--m_size;
		return s;
	}

	constexpr void release_slot(eng::u16 s) noexcept {
		m_slots[s].next = m_free;
		m_free = s;
	}

	constexpr void unlink(eng::u16 s) noexcept {
		const eng::u16 p = m_slots[s].prev;
		const eng::u16 n = m_slots[s].next;
		if (p != no_slot) {
			m_slots[p].next = n;
		} else {
			m_head = n;
		}
		if (n != no_slot) {
			m_slots[n].prev = p;
		} else {
			m_tail = p;
		}
	}

	constexpr void push_front(eng::u16 s) noexcept {
		m_slots[s].prev = no_slot;
		m_slots[s].next = m_head;
		if (m_head != no_slot) {
			m_slots[m_head].prev = s;
		}
		m_head = s;
		if (m_tail == no_slot) {
			m_tail = s;
		}
	}

	constexpr void touch(eng::u16 s) noexcept {
		if (s == m_head) {
			return;
		}
		unlink(s);
		push_front(s);
	}

	Slot m_slots[N] {};
	HashMap<K, eng::u16, N> m_index {};
	eng::u16 m_head = no_slot;
	eng::u16 m_tail = no_slot;
	eng::u16 m_free = 0u;
	eng::u16 m_size = 0u;
};

} // namespace eng::util
