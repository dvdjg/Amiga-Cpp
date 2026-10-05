#pragma once

/// \file index_list.hpp
/// `eng::util::IndexList<Index, Null>`: **lista doblemente enlazada por índices** (no
/// por punteros), para estructuras compactas en 68000: enlazar `(prev, next)` con dos
/// índices de 16 bits cuesta 4 bytes por nodo, frente a los 8 de dos punteros de 32.
///
/// Complementa `IntrusiveList` (`intrusive_list.hpp`): allí el enlace vive **dentro
/// del objeto** y se maneja con punteros; aquí el objeto ya se direcciona **por
/// índice** (una ranura de un pool/slot), así que la lista enlaza índices y los arrays
/// de enlaces los aporta el llamador como `Span` (memoria cruda, no observadores). Varias
/// listas pueden **compartir** el mismo par de arrays mientras un índice esté en una sola
/// a la vez: es lo que hace `LruCache`, donde una ranura está o bien en la lista de
/// recencia o bien en la de libres (nunca en ambas), de modo que ambas comparten
/// `prev`/`next` y no se duplica memoria.
///
/// La lista **no posee** los arrays de enlaces ni los nodos: `bind(prev, next)` los
/// asocia y el llamador debe mantenerlos vivos y en su sitio mientras dure la lista
/// (por eso quien los aporta suele ser `NonMovable`). Un índice debe estar **como
/// mucho en una lista** que use esos arrays.
///
/// Uso:
///   eng::u16 prev[N], next[N];
///   eng::util::IndexList<eng::u16, 0xffffu> order;
///   order.bind({prev, N}, {next, N});
///   order.push_back(3u);              // O(1)
///   order.erase(3u);                  // O(1)
///   for (eng::u16 i : order) { ... }
///
/// Verificación: HOST-414.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::util {

template <class Index, Index Null>
class IndexList {
public:
	static constexpr Index null = Null;

	constexpr IndexList() noexcept = default;

	/// Asocia los arrays `prev`/`next` (vistas, no los posee) y deja la lista vacía.
	/// Los índices deben caer dentro de ambos `Span`.
	constexpr void bind(eng::Span<Index> prev, eng::Span<Index> next) noexcept {
		m_prev = prev;
		m_next = next;
		m_head = Null;
		m_tail = Null;
		m_size = 0u;
	}

	[[nodiscard]] constexpr bool empty() const noexcept { return m_head == Null; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr Index front() const noexcept { return m_head; }
	[[nodiscard]] constexpr Index back() const noexcept { return m_tail; }
	[[nodiscard]] constexpr Index prev_of(Index i) const noexcept { return m_prev[i]; }
	[[nodiscard]] constexpr Index next_of(Index i) const noexcept { return m_next[i]; }

	/// Enlaza `i` al principio (O(1)). `i` no debe estar ya en esta lista.
	constexpr void push_front(Index i) noexcept {
		m_prev[i] = Null;
		m_next[i] = m_head;
		if (m_head != Null) {
			m_prev[m_head] = i;
		} else {
			m_tail = i;
		}
		m_head = i;
		++m_size;
	}

	/// Enlaza `i` al final (O(1)). `i` no debe estar ya en esta lista.
	constexpr void push_back(Index i) noexcept {
		m_next[i] = Null;
		m_prev[i] = m_tail;
		if (m_tail != Null) {
			m_next[m_tail] = i;
		} else {
			m_head = i;
		}
		m_tail = i;
		++m_size;
	}

	/// Desenlaza `i` (debe estar en la lista). Deja el enlace limpio.
	constexpr void erase(Index i) noexcept {
		const Index p = m_prev[i];
		const Index n = m_next[i];
		if (p != Null) {
			m_next[p] = n;
		} else {
			m_head = n;
		}
		if (n != Null) {
			m_prev[n] = p;
		} else {
			m_tail = p;
		}
		m_prev[i] = Null;
		m_next[i] = Null;
		--m_size;
	}

	/// Desenlaza y devuelve la cabeza (o `Null` si está vacía).
	constexpr Index pop_front() noexcept {
		const Index i = m_head;
		if (i != Null) {
			erase(i);
		}
		return i;
	}

	/// Desenlaza y devuelve la cola (o `Null` si está vacía).
	constexpr Index pop_back() noexcept {
		const Index i = m_tail;
		if (i != Null) {
			erase(i);
		}
		return i;
	}

	/// Mueve `i` al frente (debe estar en la lista). No-op si ya es la cabeza.
	constexpr void touch_front(Index i) noexcept {
		if (m_head == i) {
			return;
		}
		erase(i);
		push_front(i);
	}

	/// Iterador que avanza por la lista a través del array `next` (una vista).
	class iterator {
	public:
		constexpr iterator(eng::Span<const Index> next, Index cur) noexcept
			: m_next(next), m_cur(cur) {}
		[[nodiscard]] constexpr Index operator*() const noexcept { return m_cur; }
		constexpr iterator& operator++() noexcept {
			m_cur = m_next[m_cur];
			return *this;
		}
		[[nodiscard]] constexpr bool operator!=(const iterator& other) const noexcept {
			return m_cur != other.m_cur;
		}
		[[nodiscard]] constexpr bool operator==(const iterator& other) const noexcept {
			return m_cur == other.m_cur;
		}

	private:
		eng::Span<const Index> m_next {};
		Index m_cur = Null;
	};

	/// Primer índice de la lista (o `Null` si está vacía).
	[[nodiscard]] constexpr iterator begin() const noexcept {
		return iterator {m_next.as_const(), m_head};
	}
	/// Centinela de fin de recorrido (un índice `Null`).
	[[nodiscard]] constexpr iterator end() const noexcept {
		return iterator {m_next.as_const(), Null};
	}

private:
	eng::Span<Index> m_prev {};
	eng::Span<Index> m_next {};
	Index m_head = Null;
	Index m_tail = Null;
	usize m_size = 0u;
};

} // namespace eng::util
