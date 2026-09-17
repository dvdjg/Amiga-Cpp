#pragma once

/// \file vector.hpp
/// `eng::util::Vector<T, A>`: secuencia contigua de tamaño variable que **crece en
/// un asignador** (`BumpAlloc`/`ArenaAlloc`), sin `malloc` y sin copias ocultas.
///
/// Es el `std::vector` del engine con dos diferencias deliberadas:
/// - **No hay heap**: crece con el `Allocator` que le pases. Con `NullAlloc` no crece
///   (equivale a una capacidad fija); con una arena crece mientras quede hueco.
/// - **Solo tipos copiables trivialmente**: el almacenamiento no construye ni destruye
///   objetos, así que `memcpy`-style es válido y no hace falta `new` de colocación
///   (no disponible en freestanding). Es el caso de todos los tipos de valor del
///   engine (`Fixed`, `Point2s`, `Span`, descriptores POD…).
///
/// Regla de vida: reservar/crecer es de la fase `init`/carga; en `frame` un
/// `push_back` solo debe ocurrir si ya se reservó lo suficiente (devuelve `false` si
/// no cabe, nunca aborta).
///
/// Uso:
///   alignas(16) eng::u8 scratch[1024];
///   eng::util::Vector<eng::u16, eng::util::BumpAlloc> ids {eng::util::BumpAlloc{{scratch, sizeof scratch}}};
///   if (!ids.push_back(7u)) { /* sin hueco */ }

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/allocator.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

template <class T, class A = NullAlloc>
class Vector {
	static_assert(is_trivially_copyable_v<T>, "Vector: T debe ser copiable trivialmente");

public:
	using value_type = T;
	using iterator = T*;
	using const_iterator = const T*;

	constexpr Vector() noexcept = default;
	explicit constexpr Vector(A alloc) noexcept : m_alloc(alloc) {}

	Vector(const Vector&) = delete;
	Vector& operator=(const Vector&) = delete;

	constexpr Vector(Vector&& other) noexcept
		: m_data(other.m_data), m_size(other.m_size), m_cap(other.m_cap),
		  m_alloc(other.m_alloc) {
		other.m_data = nullptr;
		other.m_size = 0u;
		other.m_cap = 0u;
	}

	constexpr Vector& operator=(Vector&& other) noexcept {
		if (this != &other) {
			release();
			m_alloc = other.m_alloc;
			m_data = other.m_data;
			m_size = other.m_size;
			m_cap = other.m_cap;
			other.m_data = nullptr;
			other.m_size = 0u;
			other.m_cap = 0u;
		}
		return *this;
	}

	~Vector() { release(); }

	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr usize capacity() const noexcept { return m_cap; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size == m_cap; }

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
	[[nodiscard]] constexpr const T& front() const noexcept { return at(0u); }
	[[nodiscard]] constexpr T& back() noexcept { return at(m_size - 1u); }
	[[nodiscard]] constexpr const T& back() const noexcept { return at(m_size - 1u); }

	/// Garantiza capacidad para `n` elementos. `false` si la arena no puede.
	constexpr bool reserve(usize n) noexcept {
		if (n <= m_cap) {
			return true;
		}
		usize new_cap = m_cap != 0u ? m_cap : 4u;
		while (new_cap < n) {
			new_cap *= 2u;
		}
		return grow(new_cap);
	}

	/// Añade copiando; `false` si no hay hueco (no recorta nada).
	constexpr bool push_back(const T& value) noexcept {
		if (m_size == m_cap && !reserve(m_size + 1u)) {
			return false;
		}
		m_data[m_size] = value;
		++m_size;
		return true;
	}

	/// Construye in situ; `nullptr` si no hay hueco.
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

	/// Redimensiona a `n` elementos rellenando los nuevos con `value`.
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

	/// Inserta en `index` desplazando el resto (O(n)); `false` si no hay hueco.
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

	/// Borra el elemento `index` desplazando el resto.
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
	/// Libera el bloque actual (no-op en asignadores bump) y deja el vector vacío.
	constexpr void release() noexcept {
		if (m_data != nullptr) {
			m_alloc.deallocate(
				Span<u8> {reinterpret_cast<u8*>(m_data), m_cap * sizeof(T)});
		}
		m_data = nullptr;
		m_size = 0u;
		m_cap = 0u;
	}

	/// Cambia el almacenamiento a `new_cap` elementos conservando el contenido.
	constexpr bool grow(usize new_cap) noexcept {
		const Span<u8> block = m_alloc.allocate(new_cap * sizeof(T), alignof(T));
		if (block.empty()) {
			return false;
		}
		T* dst = reinterpret_cast<T*>(block.data());
		for (usize i = 0; i < m_size; ++i) {
			dst[i] = m_data[i];
		}
		if (m_data != nullptr) {
			m_alloc.deallocate(
				Span<u8> {reinterpret_cast<u8*>(m_data), m_cap * sizeof(T)});
		}
		m_data = dst;
		m_cap = new_cap;
		return true;
	}

	T* m_data = nullptr;
	usize m_size = 0u;
	usize m_cap = 0u;
	A m_alloc {};
};

} // namespace eng::util
