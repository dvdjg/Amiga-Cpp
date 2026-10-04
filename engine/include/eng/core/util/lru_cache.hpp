#pragma once

/// \file lru_cache.hpp
/// `eng::util::LruCache<K, V, N>`: **caché LRU de capacidad fija** (sin heap) con
/// `get`/`put`/`erase` en `O(1)`. Generaliza el patrón de `eng::playfield::ChunkCache`
/// (tiles, sprites, mapas): índice hash `clave -> ranura` + lista doblemente enlazada
/// sobre las ranuras para la recencia. Al insertar con la caché llena se **desaloja la
/// entrada menos usada recientemente** (la cola).
///
/// La lista de recencia y la de ranuras libres son dos `IndexList` (ver
/// `index_list.hpp`) que **comparten** los arrays `prev`/`next`: una ranura está o
/// bien en la lista de recencia o bien en la de libres, nunca en las dos, así que
/// enlazar por índices de 16 bits cuesta lo mismo que los dos enlaces por ranura y evita
/// punteros. Por eso la caché es `NonMovable`: las listas referencian sus arrays
/// internos y ni copiar ni mover el objeto sería seguro.
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

#include <eng/core/types/types.hpp>
#include <eng/core/util/hash_map.hpp>
#include <eng/core/util/index_list.hpp>
#include <eng/core/util/noncopyable.hpp>

namespace eng::util {

template <class K, class V, eng::u16 N>
class LruCache : public NonMovable {
	static_assert(N > 0u, "LruCache: N debe ser mayor que 0");
	static_assert(N < 0xffffu, "LruCache: N debe dejar libre el centinela 0xffff");

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
		const eng::Span<eng::u16> links_prev {m_prev, N};
		const eng::Span<eng::u16> links_next {m_next, N};
		m_order.bind(links_prev, links_next);
		m_free.bind(links_prev, links_next);
		eng::u16 i = N;
		while (i > 0u) {
			--i;
			m_free.push_front(i); // todas las ranuras arrancan libres
		}
	}

	/// Valor de `key`, marcándolo como el más reciente. `nullptr` si no está.
	[[nodiscard]] constexpr V* get(const K& key) noexcept {
		const eng::u16* slot = m_index.find(key);
		if (slot == nullptr) {
			return nullptr;
		}
		m_order.touch_front(*slot);
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
			m_order.touch_front(*existing);
			return false;
		}
		eng::u16 slot = no_slot;
		if (!m_free.empty()) {
			slot = m_free.pop_front();
		} else {
			slot = m_order.back(); // la menos reciente
			m_index.erase(m_slots[slot].key);
			m_order.erase(slot);
			--m_size;
		}
		m_slots[slot].key = key;
		m_slots[slot].value = value;
		m_order.push_front(slot);
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
		m_order.erase(s);
		m_free.push_front(s);
		--m_size;
		return true;
	}

private:
	struct Slot {
		K key {};
		V value {};
	};

	Slot m_slots[N] {};
	/// Enlaces `prev`/`next` compartidos por la lista de recencia y la de libres.
	eng::u16 m_prev[N] {};
	eng::u16 m_next[N] {};
	HashMap<K, eng::u16, N> m_index {};
	IndexList<eng::u16, no_slot> m_order {};
	IndexList<eng::u16, no_slot> m_free {};
	eng::u16 m_size = 0u;
};

} // namespace eng::util
