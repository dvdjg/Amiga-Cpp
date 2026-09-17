#pragma once

/// \file small_vector.hpp
/// `eng::util::SmallVector<T, N, A>`: vector con **almacenamiento inline para `N`**
/// elementos que, si crece, pide el resto a un asignador (`BumpAlloc`/`ArenaAlloc`).
///
/// Es el tipo de "lista de trabajo" del engine: casi siempre cabe en `N` (y entonces
/// no toca ninguna arena, válido incluso en `frame`) y solo en el peor caso reserva.
/// Patrón `SmallVector` de LLVM adaptado a los dos límites del engine: sin heap y
/// tipos copiables trivialmente (el almacenamiento no construye objetos).
///
/// Uso:
///   eng::util::SmallVector<Command, 8, eng::util::BumpAlloc> cmds {alloc};
///   cmds.push_back(cmd);            // hasta 8 sin tocar la arena

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/allocator.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

template <class T, usize N, class A = NullAlloc>
class SmallVector {
	static_assert(N > 0u, "SmallVector: N debe ser mayor que 0");
	static_assert(is_trivially_copyable_v<T>, "SmallVector: T debe ser copiable trivialmente");

public:
	using value_type = T;
	using iterator = T*;
	using const_iterator = const T*;

	constexpr SmallVector() noexcept = default;
	explicit constexpr SmallVector(A alloc) noexcept : m_alloc(alloc) {}

	SmallVector(const SmallVector&) = delete;
	SmallVector& operator=(const SmallVector&) = delete;

	constexpr SmallVector(SmallVector&& other) noexcept : m_alloc(other.m_alloc) {
		move_from(other);
	}
	constexpr SmallVector& operator=(SmallVector&& other) noexcept {
		if (this != &other) {
			release();
			m_alloc = other.m_alloc;
			move_from(other);
		}
		return *this;
	}
	~SmallVector() { release(); }

	[[nodiscard]] static constexpr usize inline_capacity() noexcept { return N; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr usize capacity() const noexcept { return m_cap; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size == m_cap; }
	/// ¿Sigue usando solo el almacenamiento inline? (no ha tocado el asignador)
	[[nodiscard]] constexpr bool is_inline() const noexcept {
		return m_data == reinterpret_cast<const T*>(m_storage);
	}

	[[nodiscard]] constexpr T* data() noexcept { return m_data; }
	[[nodiscard]] constexpr const T* data() const noexcept { return m_data; }
	[[nodiscard]] constexpr T* begin() noexcept { return m_data; }
	[[nodiscard]] constexpr T* end() noexcept { return m_data + m_size; }
	[[nodiscard]] constexpr const T* begin() const noexcept { return m_data; }
	[[nodiscard]] constexpr const T* end() const noexcept { return m_data + m_size; }
	[[nodiscard]] constexpr Span<T> span() noexcept { return Span<T> {m_data, m_size}; }
	[[nodiscard]] constexpr Span<const T> span() const noexcept {
		return Span<const T> {m_data, m_size};
	}

	[[nodiscard]] constexpr T& operator[](usize index) noexcept { return m_data[index]; }
	[[nodiscard]] constexpr const T& operator[](usize index) const noexcept {
		return m_data[index];
	}
	[[nodiscard]] constexpr T& at(usize index) noexcept {
		if (index >= m_size) {
			eng::detail::span_out_of_bounds();
		}
		return m_data[index];
	}
	[[nodiscard]] constexpr const T& at(usize index) const noexcept {
		if (index >= m_size) {
			eng::detail::span_out_of_bounds();
		}
		return m_data[index];
	}
	[[nodiscard]] constexpr T& front() noexcept { return at(0u); }
	[[nodiscard]] constexpr T& back() noexcept { return at(m_size - 1u); }

	constexpr bool reserve(usize n) noexcept {
		if (n <= m_cap) {
			return true;
		}
		if (is_inline()) {
			return grow(n); // primer desbordamiento: pide al asignador
		}
		usize new_cap = m_cap * 2u;
		while (new_cap < n) {
			new_cap *= 2u;
		}
		return grow(new_cap);
	}

	constexpr bool push_back(const T& value) noexcept {
		if (m_size == m_cap && !reserve(m_size + 1u)) {
			return false;
		}
		m_data[m_size] = value;
		++m_size;
		return true;
	}

	template <class... Args>
	constexpr T* emplace_back(Args&&... args) noexcept {
		if (m_size == m_cap && !reserve(m_size + 1u)) {
			return nullptr;
		}
		m_data[m_size] = T(forward<Args>(args)...);
		T* slot = &m_data[m_size];
		++m_size;
		return slot;
	}

	constexpr void pop_back() noexcept {
		if (m_size == 0u) {
			eng::detail::span_out_of_bounds();
		}
		--m_size;
	}

	constexpr void clear() noexcept { m_size = 0u; }

	constexpr bool resize(usize n, const T& value) noexcept {
		if (n > m_size && !reserve(n)) {
			return false;
		}
		while (m_size < n) {
			m_data[m_size++] = value;
		}
		m_size = n;
		return true;
	}

	constexpr bool insert(usize index, const T& value) noexcept {
		if (index > m_size) {
			eng::detail::span_out_of_bounds();
		}
		if (m_size == m_cap && !reserve(m_size + 1u)) {
			return false;
		}
		for (usize i = m_size; i > index; --i) {
			m_data[i] = m_data[i - 1u];
		}
		m_data[index] = value;
		++m_size;
		return true;
	}

	constexpr void erase(usize index) noexcept {
		if (index >= m_size) {
			eng::detail::span_out_of_bounds();
		}
		for (usize i = index; i + 1u < m_size; ++i) {
			m_data[i] = m_data[i + 1u];
		}
		--m_size;
	}

private:
	[[nodiscard]] constexpr T* inline_ptr() noexcept { return reinterpret_cast<T*>(m_storage); }

	constexpr void move_from(SmallVector& other) noexcept {
		if (other.is_inline()) {
			m_data = inline_ptr();
			m_cap = N;
			m_size = other.m_size;
			for (usize i = 0; i < m_size; ++i) {
				m_data[i] = other.m_data[i];
			}
		} else {
			m_data = other.m_data;
			m_cap = other.m_cap;
			m_size = other.m_size;
			other.m_data = other.inline_ptr();
			other.m_cap = N;
		}
		other.m_size = 0u;
	}

	constexpr void release() noexcept {
		if (!is_inline() && m_data != nullptr) {
			m_alloc.deallocate(
				Span<u8> {reinterpret_cast<u8*>(m_data), m_cap * sizeof(T)});
		}
		m_data = inline_ptr();
		m_size = 0u;
		m_cap = N;
	}

	constexpr bool grow(usize new_cap) noexcept {
		const Span<u8> block = m_alloc.allocate(new_cap * sizeof(T), alignof(T));
		if (block.empty()) {
			return false;
		}
		T* dst = reinterpret_cast<T*>(block.data());
		for (usize i = 0; i < m_size; ++i) {
			dst[i] = m_data[i];
		}
		if (!is_inline()) {
			m_alloc.deallocate(
				Span<u8> {reinterpret_cast<u8*>(m_data), m_cap * sizeof(T)});
		}
		m_data = dst;
		m_cap = new_cap;
		return true;
	}

	alignas(T) u8 m_storage[N * sizeof(T)] {};
	T* m_data = reinterpret_cast<T*>(m_storage);
	usize m_size = 0u;
	usize m_cap = N;
	A m_alloc {};
};

} // namespace eng::util
