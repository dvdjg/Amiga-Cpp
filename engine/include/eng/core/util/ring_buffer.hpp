#pragma once

/// \file ring_buffer.hpp
/// `eng::util::RingBuffer<T, N>`: cola circular de capacidad fija.
///
/// Cola FIFO sin reserva dinámica para los flujos donde el productor y el
/// consumidor van a ritmos distintos: eventos de input, muestras hacia el mezclador,
/// órdenes de dibujo, mensajes de depuración. El almacenamiento es inline (`N`
/// elementos) y los índices se ajustan sin módulo cuando el hueco lo permite.
///
/// Política de lleno explícita en el nombre:
/// - `push` **rechaza** si está lleno (`false`): el productor decide si descarta.
/// - `push_overwrite` **descarta el más antiguo** y siempre acepta.
///
/// Uso:
///   eng::util::RingBuffer<Event, 16> queue;
///   if (!queue.push(ev)) { /* lleno: descartar o crecer fuera */ }
///   Event e = queue.pop();

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/util.hpp>

namespace eng::util {

template <class T, usize N>
class RingBuffer {
	static_assert(N > 0u, "RingBuffer: N debe ser mayor que 0");

public:
	using value_type = T;

	constexpr RingBuffer() noexcept = default;

	[[nodiscard]] static constexpr usize capacity() noexcept { return N; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size == N; }

	/// Descarta todos los elementos (no toca el almacenamiento).
	constexpr void clear() noexcept {
		m_size = 0u;
		m_head = 0u;
	}

	/// Encola copiando; `false` si está lleno.
	constexpr bool push(const T& value) {
		if (full()) {
			return false;
		}
		m_data[write_index()] = value;
		++m_size;
		return true;
	}

	/// Encola moviendo; `false` si está lleno.
	constexpr bool push(T&& value) {
		if (full()) {
			return false;
		}
		m_data[write_index()] = move(value);
		++m_size;
		return true;
	}

	/// Encola por **delante** copiando (doble cola); `false` si está lleno.
	constexpr bool push_front(const T& value) {
		if (full()) {
			return false;
		}
		m_head = (m_head == 0u) ? (N - 1u) : (m_head - 1u);
		m_data[m_head] = value;
		++m_size;
		return true;
	}

	/// Encola por delante moviendo; `false` si está lleno.
	constexpr bool push_front(T&& value) {
		if (full()) {
			return false;
		}
		m_head = (m_head == 0u) ? (N - 1u) : (m_head - 1u);
		m_data[m_head] = move(value);
		++m_size;
		return true;
	}

	/// Encola descartando el elemento más antiguo si estaba lleno.
	constexpr void push_overwrite(const T& value) {
		if (full()) {
			m_data[m_head] = value;
			advance(m_head);
			return;
		}
		push(value);
	}

	/// Encola moviendo y descartando el más antiguo si estaba lleno.
	constexpr void push_overwrite(T&& value) {
		if (full()) {
			m_data[m_head] = move(value);
			advance(m_head);
			return;
		}
		push(move(value));
	}

	/// Saca el más antiguo (precondición: no vacío).
	[[nodiscard]] constexpr T pop() {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		T value = move(m_data[m_head]);
		advance(m_head);
		--m_size;
		return value;
	}

	/// Saca el más antiguo descartándolo.
	constexpr void pop_discard() noexcept {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		advance(m_head);
		--m_size;
	}

	/// Saca el más reciente (doble cola; precondición: no vacío).
	[[nodiscard]] constexpr T pop_back() {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		T value = move(m_data[index_of(m_size - 1u)]);
		--m_size;
		return value;
	}

	/// Saca el más reciente descartándolo.
	constexpr void pop_back_discard() noexcept {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		--m_size;
	}

	[[nodiscard]] constexpr T& front() noexcept {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		return m_data[m_head];
	}
	[[nodiscard]] constexpr const T& front() const noexcept {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		return m_data[m_head];
	}
	[[nodiscard]] constexpr T& back() noexcept {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		return m_data[index_of(m_size - 1u)];
	}
	[[nodiscard]] constexpr const T& back() const noexcept {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		return m_data[index_of(m_size - 1u)];
	}

	/// Acceso por orden de antigüedad: `[0]` es el más antiguo.
	[[nodiscard]] constexpr T& operator[](usize i) noexcept { return m_data[index_of(i)]; }
	[[nodiscard]] constexpr const T& operator[](usize i) const noexcept {
		return m_data[index_of(i)];
	}

private:
	/// Posición física del siguiente hueco de escritura.
	[[nodiscard]] constexpr usize write_index() const noexcept {
		const usize i = m_head + m_size;
		return i >= N ? i - N : i;
	}

	/// Posición física del elemento lógico `i` (0 = más antiguo).
	[[nodiscard]] constexpr usize index_of(usize i) const noexcept {
		const usize p = m_head + i;
		return p >= N ? p - N : p;
	}

	/// Avanza un índice circular (el llamador garantiza `i < N`).
	static constexpr void advance(usize& i) noexcept { i = (i + 1u == N) ? 0u : i + 1u; }

	T m_data[N] {};
	usize m_head = 0u;
	usize m_size = 0u;
};

} // namespace eng::util
