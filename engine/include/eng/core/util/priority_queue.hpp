#pragma once

/// \file priority_queue.hpp
/// `eng::util::PriorityQueue<T, N, Cmp>`: **cola de prioridad** de capacidad fija
/// (heap binario sobre almacenamiento inline), sin heap de memoria.
///
/// El elemento de mayor prioridad según `Cmp` queda en `top()`. Por defecto es un
/// **max-heap** (`Less<T>`: `a < b`); para un min-heap usa `Greater<T>` o tu propio
/// comparador (p. ej. por el `key` de un `SortItem`). `push`/`pop` son `O(log N)`,
/// `top` es `O(1)`.
///
/// No asigna: la capacidad es `N` y `push` devuelve `false` si está llena. `T` debe
/// ser construible por defecto y asignable. Base de scheduling, IA/A* y colas de
/// eventos donde importa el orden y no el orden de llegada.
///
/// Uso:
///   eng::util::PriorityQueue<int, 8> pq;          // max-heap
///   pq.push(3); pq.push(9); pq.push(1);
///   pq.top();   // 9
///   pq.pop();
///   eng::util::PriorityQueue<int, 8, eng::util::Greater<int>> minq;  // min-heap

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/type_traits.hpp>
#include <eng/core/util/util.hpp>

namespace eng::util {

/// Comparador por defecto: `a` tiene menos prioridad que `b` (`a < b`).
template <class T>
struct Less {
	[[nodiscard]] constexpr bool operator()(const T& a, const T& b) const noexcept {
		return a < b;
	}
};

/// Comparador inverso (para min-heap sobre un `operator<`).
template <class T>
struct Greater {
	[[nodiscard]] constexpr bool operator()(const T& a, const T& b) const noexcept {
		return b < a;
	}
};

template <class T, usize N, class Cmp = Less<T>>
class PriorityQueue {
	static_assert(N > 0u, "PriorityQueue: N debe ser mayor que 0");

public:
	constexpr PriorityQueue() noexcept = default;

	[[nodiscard]] static constexpr usize capacity() noexcept { return N; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size == N; }

	constexpr void clear() noexcept { m_size = 0u; }

	/// Elemento de mayor prioridad (precondición: no vacía; violación → `illegal`).
	[[nodiscard]] constexpr T& top() noexcept {
		if (m_size == 0u) {
			eng::detail::span_out_of_bounds();
		}
		return m_data[0];
	}
	[[nodiscard]] constexpr const T& top() const noexcept {
		if (m_size == 0u) {
			eng::detail::span_out_of_bounds();
		}
		return m_data[0];
	}

	/// Añade copiando; `false` si está llena.
	constexpr bool push(const T& value) noexcept {
		if (m_size == N) {
			return false;
		}
		m_data[m_size] = value;
		sift_up(m_size);
		++m_size;
		return true;
	}

	/// Construye in situ y lo añade; `false` si está llena.
	template <class... Args>
	constexpr bool emplace(Args&&... args) noexcept {
		if (m_size == N) {
			return false;
		}
		m_data[m_size] = T(forward<Args>(args)...);
		sift_up(m_size);
		++m_size;
		return true;
	}

	/// Quita el elemento de mayor prioridad (precondición: no vacía).
	constexpr void pop() noexcept {
		if (m_size == 0u) {
			eng::detail::span_out_of_bounds();
		}
		--m_size;
		if (m_size != 0u) {
			m_data[0] = m_data[m_size];
			sift_down(0u);
		}
	}

private:
	constexpr void sift_up(usize i) noexcept {
		while (i > 0u) {
			const usize parent = (i - 1u) / 2u;
			if (!m_cmp(m_data[parent], m_data[i])) {
				break; // el padre ya tiene prioridad >= que el hijo
			}
			swap(m_data[parent], m_data[i]);
			i = parent;
		}
	}

	constexpr void sift_down(usize i) noexcept {
		for (;;) {
			const usize left = 2u * i + 1u;
			const usize right = 2u * i + 2u;
			usize best = i;
			if (left < m_size && m_cmp(m_data[best], m_data[left])) {
				best = left;
			}
			if (right < m_size && m_cmp(m_data[best], m_data[right])) {
				best = right;
			}
			if (best == i) {
				break;
			}
			swap(m_data[i], m_data[best]);
			i = best;
		}
	}

	T m_data[N] {};
	usize m_size = 0u;
	Cmp m_cmp {};
};

} // namespace eng::util
