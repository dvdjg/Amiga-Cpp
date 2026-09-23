#pragma once

/// \file static_vector.hpp
/// `eng::util::StaticVector<T, N>`: secuencia de tamaño variable con capacidad
/// fija (`N` elementos inline, sin reserva dinámica).
///
/// El engine prohíbe asignar memoria durante el gameplay y trabaja con "arrays más
/// un contador" (listas de actores, intents, tiles de un burst, celdas sucias). Este
/// tipo encapsula ese patrón: el almacenamiento vive dentro del objeto y el tamaño
/// lógico lo lleva `size()`.
///
/// Requisitos y coste (visibles a propósito):
/// - `T` debe ser **construible por defecto**: los `N` huecos se construyen al
///   declarar el objeto. Para tipos ligeros (enteros, `Point2s`, `Fixed`, structs
///   pequeños) el coste es despreciable; para un `T` pesado, usar un pool/arena.
/// - `push_back`/`emplace_back` no reservan nunca; si está lleno, `push_back`
///   devuelve `false` y `emplace_back` `nullptr` en vez de desbordar.
///
/// Uso:
///   eng::util::StaticVector<Actor, 32> actors;
///   if (!actors.push_back(actor)) { /* sin hueco */ }
///   eng::util::for_each(actors.span(), [](Actor& a) { a.step(); });
///
/// Verificación: HOST-077 y demo `086_bob_objects` (`emit_bob_fallbacks`,
/// `build -> run -> analyze` OK).

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/type_traits.hpp>
#include <eng/core/util/util.hpp>

namespace eng::util {

template <class T, usize N>
class StaticVector {
	static_assert(N > 0u, "StaticVector: N debe ser mayor que 0");

public:
	using value_type = T;
	using iterator = T*;
	using const_iterator = const T*;

	constexpr StaticVector() noexcept = default;

	[[nodiscard]] static constexpr usize capacity() noexcept { return N; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size == N; }

	[[nodiscard]] constexpr T& operator[](usize index) noexcept { return m_data[index]; }
	[[nodiscard]] constexpr const T& operator[](usize index) const noexcept { return m_data[index]; }

	/// Acceso con comprobación de rango (violación → `illegal` en m68k).
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

	[[nodiscard]] constexpr T& front() noexcept {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		return m_data[0];
	}
	[[nodiscard]] constexpr const T& front() const noexcept {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		return m_data[0];
	}
	[[nodiscard]] constexpr T& back() noexcept {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		return m_data[m_size - 1u];
	}
	[[nodiscard]] constexpr const T& back() const noexcept {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		return m_data[m_size - 1u];
	}

	[[nodiscard]] constexpr T* data() noexcept { return m_data; }
	[[nodiscard]] constexpr const T* data() const noexcept { return m_data; }

	[[nodiscard]] constexpr iterator begin() noexcept { return m_data; }
	[[nodiscard]] constexpr iterator end() noexcept { return m_data + m_size; }
	[[nodiscard]] constexpr const_iterator begin() const noexcept { return m_data; }
	[[nodiscard]] constexpr const_iterator end() const noexcept { return m_data + m_size; }

	/// Vista sobre los elementos vivos (para las APIs que piden `Span`).
	[[nodiscard]] constexpr Span<T> span() noexcept { return Span<T> {m_data, m_size}; }
	[[nodiscard]] constexpr Span<const T> span() const noexcept {
		return Span<const T> {m_data, m_size};
	}

	/// Añade copiando; `false` si está lleno.
	constexpr bool push_back(const T& value) {
		if (full()) {
			return false;
		}
		m_data[m_size] = value;
		++m_size;
		return true;
	}

	/// Añade moviendo; `false` si está lleno.
	constexpr bool push_back(T&& value) {
		if (full()) {
			return false;
		}
		m_data[m_size] = move(value);
		++m_size;
		return true;
	}

	/// Construye in situ en el hueco libre; `nullptr` si está lleno.
	template <class... Args>
	constexpr T* emplace_back(Args&&... args) {
		if (full()) {
			return nullptr;
		}
		m_data[m_size] = T(forward<Args>(args)...);
		T* slot = &m_data[m_size];
		++m_size;
		return slot;
	}

	/// Quita el último elemento (precondición: no vacío).
	constexpr void pop_back() noexcept {
		if (empty()) {
			eng::detail::span_out_of_bounds();
		}
		--m_size;
	}

	/// Quita todos los elementos (no toca el almacenamiento).
	constexpr void clear() noexcept { m_size = 0u; }

	/// Borra el elemento `index` desplazando el resto (mantiene el orden).
	constexpr void erase(usize index) noexcept {
		if (index >= m_size) {
			eng::detail::span_out_of_bounds();
		}
		for (usize i = index; i + 1u < m_size; ++i) {
			m_data[i] = move(m_data[i + 1u]);
		}
		--m_size;
	}

	/// Borra el elemento apuntado por `pos` (debe pertenecer a la vista). Se
	/// restringe a punteros para que `erase(0)` no sea ambiguo con `erase(usize)`.
	template <class P, enable_if_t<is_pointer_v<P>, int> = 0>
	constexpr void erase(P pos) noexcept {
		erase(static_cast<usize>(pos - m_data));
	}

	/// Escribe `value` en todos los elementos vivos.
	constexpr void fill(const T& value) {
		for (usize i = 0; i < m_size; ++i) {
			m_data[i] = value;
		}
	}

private:
	T m_data[N] {};
	usize m_size = 0u;
};

} // namespace eng::util
