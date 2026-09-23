#pragma once

/// \file stack_queue.hpp
/// **Vocabulario explícito** para las estructuras clásicas (`eng::util`): `Stack`
/// (LIFO), `Queue` (FIFO) y `Deque` (doble cola), con capacidad fija y sin heap.
///
/// Son envoltorios finos sobre los contenedores que ya existen (`StaticVector` y
/// `RingBuffer`, este último ampliado con `push_front`/`pop_back`), para que el código
/// diga qué estructura usa en vez de "un vector que uso como pila". Coste cero: no
/// añaden datos ni indirección.
///
/// Uso:
///   eng::util::Stack<int, 16> calls;
///   eng::util::Queue<int, 16> events;
///   eng::util::Deque<int, 16> work;   // push_front / push_back

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/ring_buffer.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/core/util/util.hpp>

namespace eng::util {

/// Pila LIFO: `push`/`pop` por el mismo extremo (`top`).
template <class T, usize N>
class Stack {
public:
	[[nodiscard]] static constexpr usize capacity() noexcept { return N; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_data.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_data.empty(); }
	[[nodiscard]] constexpr bool full() const noexcept { return m_data.full(); }

	constexpr bool push(const T& value) { return m_data.push_back(value); }
	constexpr bool push(T&& value) { return m_data.push_back(move(value)); }
	template <class... Args>
	constexpr T* emplace(Args&&... args) {
		return m_data.emplace_back(forward<Args>(args)...);
	}
	constexpr void pop() { m_data.pop_back(); }

	[[nodiscard]] constexpr T& top() noexcept { return m_data.back(); }
	[[nodiscard]] constexpr const T& top() const noexcept { return m_data.back(); }

	constexpr void clear() noexcept { m_data.clear(); }
	[[nodiscard]] constexpr Span<T> span() noexcept { return m_data.span(); }

private:
	StaticVector<T, N> m_data {};
};

/// Cola FIFO: entra por detrás (`push`) y sale por delante (`pop`/`front`).
template <class T, usize N>
class Queue {
public:
	[[nodiscard]] static constexpr usize capacity() noexcept { return N; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_data.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_data.empty(); }
	[[nodiscard]] constexpr bool full() const noexcept { return m_data.full(); }

	constexpr bool push(const T& value) { return m_data.push(value); }
	constexpr bool push(T&& value) { return m_data.push(move(value)); }

	/// Saca el elemento más antiguo (precondición: no vacía).
	[[nodiscard]] constexpr T pop() { return m_data.pop(); }
	constexpr void pop_discard() noexcept { m_data.pop_discard(); }

	[[nodiscard]] constexpr T& front() noexcept { return m_data.front(); }
	[[nodiscard]] constexpr const T& front() const noexcept { return m_data.front(); }
	[[nodiscard]] constexpr T& back() noexcept { return m_data.back(); }
	[[nodiscard]] constexpr const T& back() const noexcept { return m_data.back(); }

	constexpr void clear() noexcept { m_data.clear(); }

private:
	RingBuffer<T, N> m_data {};
};

/// Cola **doble**: entra y sale por ambos extremos. `operator[0]` es el más antiguo.
template <class T, usize N>
class Deque {
public:
	[[nodiscard]] static constexpr usize capacity() noexcept { return N; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_data.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_data.empty(); }
	[[nodiscard]] constexpr bool full() const noexcept { return m_data.full(); }

	constexpr bool push_back(const T& value) { return m_data.push(value); }
	constexpr bool push_back(T&& value) { return m_data.push(move(value)); }
	constexpr bool push_front(const T& value) { return m_data.push_front(value); }
	constexpr bool push_front(T&& value) { return m_data.push_front(move(value)); }

	[[nodiscard]] constexpr T pop_front() { return m_data.pop(); }
	[[nodiscard]] constexpr T pop_back() { return m_data.pop_back(); }
	constexpr void pop_front_discard() noexcept { m_data.pop_discard(); }
	constexpr void pop_back_discard() noexcept { m_data.pop_back_discard(); }

	[[nodiscard]] constexpr T& front() noexcept { return m_data.front(); }
	[[nodiscard]] constexpr const T& front() const noexcept { return m_data.front(); }
	[[nodiscard]] constexpr T& back() noexcept { return m_data.back(); }
	[[nodiscard]] constexpr const T& back() const noexcept { return m_data.back(); }
	[[nodiscard]] constexpr T& operator[](usize i) noexcept { return m_data[i]; }
	[[nodiscard]] constexpr const T& operator[](usize i) const noexcept { return m_data[i]; }

	constexpr void clear() noexcept { m_data.clear(); }

private:
	RingBuffer<T, N> m_data {};
};

} // namespace eng::util
