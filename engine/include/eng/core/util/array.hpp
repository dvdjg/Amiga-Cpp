#pragma once

/// \file array.hpp
/// `eng::util::Array<T, N>`: array contiguo de tamaño fijo (`std::array`
/// freestanding).
///
/// A diferencia de `eng::ct_array` (una tabla generada en compilación por un
/// functor), `Array` es un **agregado** que se inicia con llaves como un array
/// C, pero con tamaño, iteradores y comprobación de rango. Su razón de ser en el
/// engine es eliminar el patrón `T v[N]` desnudo: el tamaño viaja con el objeto y
/// un índice fuera de rango se detecta en el punto del fallo en vez de corromper
/// memoria.
///
/// Coste: exactamente `N * sizeof(T)` bytes, sin reserva dinámica ni código extra
/// si no se usa `at()`. `at()` dispara `illegal` (0x4afc) en m68k, igual que
/// `Span::at`, lo que convierte el fallo en un alto detectable por el emulador.
///
/// Uso:
///   eng::util::Array<eng::u16, 4> offsets { { 0, 16, 16, 48 } };
///   for (eng::u16 o : offsets) { ... }
///   const eng::u16 first = offsets.front();

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::util {

template <class T, usize N>
struct Array {
	T elems[N];

	[[nodiscard]] static constexpr usize size() noexcept { return N; }
	[[nodiscard]] static constexpr bool empty() noexcept { return N == 0u; }
	[[nodiscard]] static constexpr usize capacity() noexcept { return N; }

	/// Acceso directo, coste cero (como `Span::operator[]`).
	[[nodiscard]] constexpr T& operator[](usize index) noexcept { return elems[index]; }
	[[nodiscard]] constexpr const T& operator[](usize index) const noexcept { return elems[index]; }

	/// Acceso con comprobación de rango; violación → `illegal` en m68k.
	[[nodiscard]] constexpr T& at(usize index) noexcept {
		if (index >= N) {
			eng::detail::span_out_of_bounds();
		}
		return elems[index];
	}
	[[nodiscard]] constexpr const T& at(usize index) const noexcept {
		if (index >= N) {
			eng::detail::span_out_of_bounds();
		}
		return elems[index];
	}

	[[nodiscard]] constexpr T& front() noexcept { return elems[0]; }
	[[nodiscard]] constexpr const T& front() const noexcept { return elems[0]; }
	[[nodiscard]] constexpr T& back() noexcept { return elems[N - 1u]; }
	[[nodiscard]] constexpr const T& back() const noexcept { return elems[N - 1u]; }

	[[nodiscard]] constexpr T* data() noexcept { return elems; }
	[[nodiscard]] constexpr const T* data() const noexcept { return elems; }

	[[nodiscard]] constexpr T* begin() noexcept { return elems; }
	[[nodiscard]] constexpr T* end() noexcept { return elems + N; }
	[[nodiscard]] constexpr const T* begin() const noexcept { return elems; }
	[[nodiscard]] constexpr const T* end() const noexcept { return elems + N; }
	[[nodiscard]] constexpr const T* cbegin() const noexcept { return elems; }
	[[nodiscard]] constexpr const T* cend() const noexcept { return elems + N; }

	/// Escribe `value` en todos los elementos.
	constexpr void fill(const T& value) {
		for (usize i = 0; i < N; ++i) {
			elems[i] = value;
		}
	}

	/// Intercambia el contenido con otro `Array` del mismo tipo y tamaño.
	constexpr void swap(Array& other) {
		for (usize i = 0; i < N; ++i) {
			T tmp = elems[i];
			elems[i] = other.elems[i];
			other.elems[i] = tmp;
		}
	}

	/// Vista mutable/const sobre el contenido (para las APIs que piden `Span`).
	[[nodiscard]] constexpr Span<T> span() noexcept { return Span<T> {elems, N}; }
	[[nodiscard]] constexpr Span<const T> span() const noexcept { return Span<const T> {elems, N}; }

	[[nodiscard]] friend constexpr bool operator==(const Array& a, const Array& b) {
		for (usize i = 0; i < N; ++i) {
			if (!(a.elems[i] == b.elems[i])) {
				return false;
			}
		}
		return true;
	}
};

} // namespace eng::util
