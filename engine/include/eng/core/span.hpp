#pragma once

/// \file span.hpp
/// Vista contigua con tamaño (equivalente freestanding a `std::span`).
///
/// El runtime Amiga del engine es freestanding (`-nostdlib`, sin excepciones,
/// sin STL hosted), asi que no podemos usar `std::span`. Esta clase aporta las
/// mismas garantias de seguridad que justifican `std::span` sin depender de la
/// libreria estandar:
///
/// - el tamaño viaja con la vista: es imposible pasar un puntero y un contador
///   que no coincidan (el fallo clasico de `clear_bytes(u8*, u32)`);
/// - `operator[]` tiene coste cero (igual que `std::span`);
/// - `at()` comprueba el indice y, en caso de violacion, dispara `illegal`
///   (0x4afc) en m68k, que cualquier emulador/debugger detecta al instante;
/// - `first/last/subspan` permiten acotar vistas sin puntero suelto.
///
/// La clase no posee memoria: es la misma idea de `std::span`, pensada para
/// buffers de arenas, planos de bitplane, caches de tiles y listas de comandos
/// del Copper.

#include <eng/core/types.hpp>

namespace eng {

namespace detail {

/// Detiene la CPU ante una violacion de rango.
///
/// En m68k `__builtin_trap` emite `illegal` (0x4afc), la instruccion de depuracion
/// por excelencia: el emulador se detiene y la traza apunta al fallo exacto.
[[noreturn]] inline void span_out_of_bounds() {
	__builtin_trap();
}

} // namespace detail

/// Vista no propietaria de un rango contiguo de `T`.
template <typename T>
class Span {
public:
	using value_type = T;
	using pointer = T*;
	using const_pointer = const T*;
	using size_type = usize;
	using iterator = T*;
	using const_iterator = const T*;

	constexpr Span() noexcept = default;

	constexpr Span(pointer data, size_type count) noexcept
		: m_data(data), m_count(count) {}

	template <size_type N>
	constexpr Span(T (&array)[N]) noexcept
		: m_data(array), m_count(N) {}

	[[nodiscard]] constexpr pointer data() const noexcept { return m_data; }
	[[nodiscard]] constexpr size_type size() const noexcept { return m_count; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_count == 0; }

	[[nodiscard]] constexpr iterator begin() const noexcept { return m_data; }
	[[nodiscard]] constexpr iterator end() const noexcept { return m_data + m_count; }
	[[nodiscard]] constexpr const_iterator cbegin() const noexcept { return m_data; }
	[[nodiscard]] constexpr const_iterator cend() const noexcept { return m_data + m_count; }

	/// Acceso directo, coste cero, sin comprobar (igual que `std::span`).
	///
	/// Para los puntos de confianza usa `at()`.
	constexpr T& operator[](size_type index) const noexcept {
		return m_data[index];
	}

	/// Acceso con comprobacion de rango.
	///
	/// En caso de violacion detiene la CPU (instruccion `illegal` en m68k), lo que
	/// convierte un fallo de indice en un error inmediato y localizable en vez de
	/// una corrupcion de memoria silenciosa.
	constexpr T& at(size_type index) const noexcept {
		if (index >= m_count) {
			detail::span_out_of_bounds();
		}
		return m_data[index];
	}

	[[nodiscard]] constexpr Span<T> first(size_type count) const noexcept {
		return {m_data, count};
	}

	[[nodiscard]] constexpr Span<T> last(size_type count) const noexcept {
		return {m_data + (m_count - count), count};
	}

	[[nodiscard]] constexpr Span<T> subspan(size_type offset, size_type count) const noexcept {
		return {m_data + offset, count};
	}

	[[nodiscard]] constexpr Span<T> subspan(size_type offset) const noexcept {
		return {m_data + offset, m_count - offset};
	}

	/// Rellena todos los elementos con `value`.
	constexpr void fill(const T& value) const noexcept {
		for (size_type i = 0; i < m_count; ++i) {
			m_data[i] = value;
		}
	}

	/// Rellena con el valor por defecto de `T` (0 para enteros).
	constexpr void clear() const noexcept {
		fill(T{});
	}

	/// Vista de solo lectura sobre la misma memoria.
	///
	/// Se usa en lugar de una conversion implicita `Span<T> -> Span<const T>`
	/// porque una conversion que devuelve el mismo tipo (cuando `T` ya es `const`)
	/// es malformada en C++.
	constexpr Span<const T> as_const() const noexcept {
		return {m_data, m_count};
	}

	static constexpr Span<T> from_raw(pointer data, size_type count) noexcept {
		return {data, count};
	}

private:
	pointer m_data = nullptr;
	size_type m_count = 0;
};

/// Guia de deduccion para arrays C: `Span span = my_array;`.
template <typename T, usize N>
Span(T (&)[N]) -> Span<T>;

/// Rellena una vista con ceros.
template <typename T>
constexpr void fill_zeros(Span<T> span) noexcept {
	span.fill(T{});
}

} // namespace eng
