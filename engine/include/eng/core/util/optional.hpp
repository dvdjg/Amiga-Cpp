#pragma once

/// \file optional.hpp
/// `eng::util::Optional<T>`: valor que puede estar ausente (`std::optional`),
/// sin excepciones ni reserva dinámica.
///
/// Uso típico en el engine: resultados de búsquedas que pueden no encontrar nada
/// (`find_actor`, `next_free_slot`), parámetros opcionales de configuración y
/// cachés donde "vacío" es un estado de primera clase. Evita el par
/// `bool + valor` suelto, que se desincroniza con facilidad.
///
/// Decisión de coste (A500): el almacenamiento de `T` está **siempre presente**;
/// `Optional` añade un `bool` y no ahorra memoria. Se evita así el `new` de
/// colocación (no disponible en freestanding) y se mantiene `constexpr`. `T` debe
/// ser construible por defecto. Para tipos pesados o que no cumplen eso, usar una
/// unión dedicada en el consumidor.
///
/// `value()` sobre un `Optional` vacío es un error de programa: detiene la CPU
/// (misma parada `illegal` que `Span::at`) en vez de devolver basura.
///
/// Uso:
///   eng::util::Optional<eng::u8> slot = find_free_channel();
///   if (slot) { use(*slot); }

#include <eng/core/span.hpp>
#include <eng/core/util/type_traits.hpp>
#include <eng/core/util/util.hpp>

namespace eng::util {

namespace detail {
/// Parada ante un acceso a un `Optional`/`Expected` sin valor (en m68k, `illegal`).
[[noreturn]] inline void bad_access() { __builtin_trap(); }
} // namespace detail

template <class T>
class Optional {
public:
	using value_type = T;

	/// Vacío. `T` se construye por defecto para reservar el hueco.
	constexpr Optional() noexcept = default;

	constexpr Optional(const T& value) : m_value(value), m_has(true) {}
	constexpr Optional(T&& value) : m_value(move(value)), m_has(true) {}

	constexpr Optional& operator=(const T& value) {
		m_value = value;
		m_has = true;
		return *this;
	}
	constexpr Optional& operator=(T&& value) {
		m_value = move(value);
		m_has = true;
		return *this;
	}

	[[nodiscard]] constexpr bool has_value() const noexcept { return m_has; }
	[[nodiscard]] constexpr explicit operator bool() const noexcept { return m_has; }

	[[nodiscard]] constexpr T& value() noexcept {
		if (!m_has) {
			detail::bad_access();
		}
		return m_value;
	}
	[[nodiscard]] constexpr const T& value() const noexcept {
		if (!m_has) {
			detail::bad_access();
		}
		return m_value;
	}

	[[nodiscard]] constexpr T& operator*() noexcept { return value(); }
	[[nodiscard]] constexpr const T& operator*() const noexcept { return value(); }
	[[nodiscard]] constexpr T* operator->() noexcept { return &value(); }
	[[nodiscard]] constexpr const T* operator->() const noexcept { return &value(); }

	/// Valor si lo hay; si no, `fallback`. Requiere copia de `T`.
	[[nodiscard]] constexpr T value_or(const T& fallback) const {
		return m_has ? m_value : fallback;
	}

	/// Vacía el `Optional` (el almacenamiento de `T` sigue reservado).
	constexpr void reset() noexcept { m_has = false; }

	/// Construye el valor in situ y lo marca presente. Devuelve la referencia.
	template <class... Args>
	constexpr T& emplace(Args&&... args) {
		m_value = T(forward<Args>(args)...);
		m_has = true;
		return m_value;
	}

	constexpr void swap(Optional& other) {
		eng::util::swap(m_value, other.m_value);
		eng::util::swap(m_has, other.m_has);
	}

private:
	T m_value {};
	bool m_has = false;
};

} // namespace eng::util
