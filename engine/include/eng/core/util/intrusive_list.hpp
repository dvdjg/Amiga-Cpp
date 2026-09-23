#pragma once

/// \file intrusive_list.hpp
/// **Listas intrusivas** del engine (`eng::util`): el enlace (punteros `prev`/`next`)
/// vive **dentro del propio objeto**, así que insertar y borrar es `O(1)` **sin
/// asignar memoria**. Es lo contrario de `std::list`: en el A500 no queremos nodos con
/// heap ni una asignación por elemento.
///
/// El objeto se enlaza derivando de `IntrusiveLink<T>` (lista doble) o
/// `IntrusiveSLink<T>` (simple); la lista manipula esas conexiones y nunca posee el
/// objeto. Un mismo objeto puede estar en varias listas a la vez si deriva de varios
/// enlaces (con hooks distintos), pero cada lista necesita su propio enlace.
///
/// Uso:
///   struct Job : eng::util::IntrusiveLink<Job> { int priority; };
///   eng::util::IntrusiveList<Job> queue;
///   queue.push_back(&job);          // O(1), sin heap
///   queue.erase(&job);              // O(1) con el puntero al nodo
///   for (Job* j : queue) { ... }

#include <eng/core/types/types.hpp>

namespace eng::util {

/// Enlace de lista **doblemente** enlazada. El usuario deriva su tipo de aquí.
template <class T>
struct IntrusiveLink {
	T* prev = nullptr;
	T* next = nullptr;

	/// Reinicia el enlace (el objeto queda "suelto", no enlazado).
	constexpr void unlink() noexcept {
		prev = nullptr;
		next = nullptr;
	}
};

/// Enlace de lista **simplemente** enlazada.
template <class T>
struct IntrusiveSLink {
	T* next = nullptr;
	constexpr void unlink() noexcept { next = nullptr; }
};

/// Lista doblemente enlazada intrusiva. No posee los nodos; el llamador los mantiene
/// vivos mientras estén enlazados.
template <class T>
class IntrusiveList {
public:
	constexpr IntrusiveList() noexcept = default;

	[[nodiscard]] constexpr bool empty() const noexcept { return m_head == nullptr; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }

	[[nodiscard]] constexpr T* front() noexcept { return m_head; }
	[[nodiscard]] constexpr const T* front() const noexcept { return m_head; }
	[[nodiscard]] constexpr T* back() noexcept { return m_tail; }
	[[nodiscard]] constexpr const T* back() const noexcept { return m_tail; }

	/// Enlaza `item` al principio (O(1)). `item` no debe estar ya en la lista.
	constexpr void push_front(T* item) noexcept {
		item->prev = nullptr;
		item->next = m_head;
		if (m_head != nullptr) {
			m_head->prev = item;
		} else {
			m_tail = item;
		}
		m_head = item;
		++m_size;
	}

	/// Enlaza `item` al final (O(1)).
	constexpr void push_back(T* item) noexcept {
		item->next = nullptr;
		item->prev = m_tail;
		if (m_tail != nullptr) {
			m_tail->next = item;
		} else {
			m_head = item;
		}
		m_tail = item;
		++m_size;
	}

	/// Desenlaza la cabeza. `nullptr` si está vacía.
	constexpr T* pop_front() noexcept {
		if (m_head == nullptr) {
			return nullptr;
		}
		T* item = m_head;
		erase(item);
		return item;
	}

	/// Desenlaza la cola. `nullptr` si está vacía.
	constexpr T* pop_back() noexcept {
		if (m_tail == nullptr) {
			return nullptr;
		}
		T* item = m_tail;
		erase(item);
		return item;
	}

	/// Desenlaza `item` (O(1); debe estar en la lista). Deja el enlace limpio.
	constexpr void erase(T* item) noexcept {
		if (item->prev != nullptr) {
			item->prev->next = item->next;
		} else {
			m_head = item->next;
		}
		if (item->next != nullptr) {
			item->next->prev = item->prev;
		} else {
			m_tail = item->prev;
		}
		item->unlink();
		--m_size;
	}

	/// ¿Está `item` en la lista? (recorrido O(n); usar `erase` directo si ya se sabe).
	[[nodiscard]] constexpr bool contains(const T* item) const noexcept {
		for (const T* it = m_head; it != nullptr; it = it->next) {
			if (it == item) {
				return true;
			}
		}
		return false;
	}

	/// Desenlaza todos los nodos (no los destruye).
	constexpr void clear() noexcept {
		T* it = m_head;
		while (it != nullptr) {
			T* next = it->next;
			it->unlink();
			it = next;
		}
		m_head = nullptr;
		m_tail = nullptr;
		m_size = 0u;
	}

	class iterator {
	public:
		constexpr explicit iterator(T* cur) noexcept : m_cur(cur) {}
		[[nodiscard]] constexpr T* operator*() const noexcept { return m_cur; }
		constexpr iterator& operator++() noexcept {
			m_cur = m_cur->next;
			return *this;
		}
		[[nodiscard]] constexpr bool operator!=(const iterator& other) const noexcept {
			return m_cur != other.m_cur;
		}
		[[nodiscard]] constexpr bool operator==(const iterator& other) const noexcept {
			return m_cur == other.m_cur;
		}

	private:
		T* m_cur = nullptr;
	};

	class const_iterator {
	public:
		constexpr explicit const_iterator(const T* cur) noexcept : m_cur(cur) {}
		[[nodiscard]] constexpr const T* operator*() const noexcept { return m_cur; }
		constexpr const_iterator& operator++() noexcept {
			m_cur = m_cur->next;
			return *this;
		}
		[[nodiscard]] constexpr bool operator!=(const const_iterator& other) const noexcept {
			return m_cur != other.m_cur;
		}

	private:
		const T* m_cur = nullptr;
	};

	[[nodiscard]] constexpr iterator begin() noexcept { return iterator {m_head}; }
	[[nodiscard]] constexpr iterator end() noexcept { return iterator {nullptr}; }
	[[nodiscard]] constexpr const_iterator begin() const noexcept {
		return const_iterator {m_head};
	}
	[[nodiscard]] constexpr const_iterator end() const noexcept {
		return const_iterator {nullptr};
	}

private:
	T* m_head = nullptr;
	T* m_tail = nullptr;
	usize m_size = 0u;
};

/// Lista simplemente enlazada intrusiva (menos memoria por nodo, recorrido en un
/// sentido). Útil para free-lists y colas donde solo se avanza.
template <class T>
class IntrusiveSList {
public:
	constexpr IntrusiveSList() noexcept = default;

	[[nodiscard]] constexpr bool empty() const noexcept { return m_head == nullptr; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr T* front() noexcept { return m_head; }
	[[nodiscard]] constexpr const T* front() const noexcept { return m_head; }

	constexpr void push_front(T* item) noexcept {
		item->next = m_head;
		m_head = item;
		++m_size;
	}

	constexpr T* pop_front() noexcept {
		if (m_head == nullptr) {
			return nullptr;
		}
		T* item = m_head;
		m_head = item->next;
		item->unlink();
		--m_size;
		return item;
	}

	/// Enlaza `item` justo después de `pos` (O(1)).
	constexpr void insert_after(T* pos, T* item) noexcept {
		item->next = pos->next;
		pos->next = item;
		++m_size;
	}

	/// Desenlaza el nodo que sigue a `pos` (O(1)).
	constexpr T* erase_after(T* pos) noexcept {
		T* item = pos->next;
		if (item == nullptr) {
			return nullptr;
		}
		pos->next = item->next;
		item->unlink();
		--m_size;
		return item;
	}

	[[nodiscard]] constexpr bool contains(const T* item) const noexcept {
		for (const T* it = m_head; it != nullptr; it = it->next) {
			if (it == item) {
				return true;
			}
		}
		return false;
	}

	constexpr void clear() noexcept {
		T* it = m_head;
		while (it != nullptr) {
			T* next = it->next;
			it->unlink();
			it = next;
		}
		m_head = nullptr;
		m_size = 0u;
	}

	class iterator {
	public:
		constexpr explicit iterator(T* cur) noexcept : m_cur(cur) {}
		[[nodiscard]] constexpr T* operator*() const noexcept { return m_cur; }
		constexpr iterator& operator++() noexcept {
			m_cur = m_cur->next;
			return *this;
		}
		[[nodiscard]] constexpr bool operator!=(const iterator& other) const noexcept {
			return m_cur != other.m_cur;
		}

	private:
		T* m_cur = nullptr;
	};

	[[nodiscard]] constexpr iterator begin() noexcept { return iterator {m_head}; }
	[[nodiscard]] constexpr iterator end() noexcept { return iterator {nullptr}; }

private:
	T* m_head = nullptr;
	usize m_size = 0u;
};

} // namespace eng::util
