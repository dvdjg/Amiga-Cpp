#pragma once

/// \file expected.hpp
/// `eng::util::Expected<T, E>`: valor **o** error (`std::expected`), sin
/// excepciones.
///
/// El engine no usa excepciones: las operaciones que pueden fallar devuelven un
/// resultado explícito. `eng::Result` (`eng/core/types.hpp`) es el enum de causas
/// sin valor asociado; `Expected<T, E>` es la versión que **transporta el valor**
/// cuando la operación tuvo éxito y el error cuando no. Es el tipo que pide
/// `PUBLIC_API.md` §5 para la frontera pública.
///
/// El error se introduce con `unexpected(e)` (patrón de `std::unexpected`), lo que
/// evita la ambigüedad cuando `T` y `E` son el mismo tipo:
///
///   Expected<Handle, Result> open(Id id) {
///       if (!valid(id)) return unexpected(Result::InvalidArgument);
///       return make_handle(id);          // éxito
///   }
///   auto r = open(id);
///   if (r) { use(r.value()); } else { log(r.error()); }
///
/// Decisión de coste (A500): igual que `Optional`, el almacenamiento de `T` y de
/// `E` está siempre presente (evita `new` de colocación y mantiene `constexpr`);
/// `T` y `E` deben ser construibles por defecto. `value()` sobre un error detiene
/// la CPU (parada `illegal` en m68k), igual que `Optional::value()`.
///
/// **Coste medido** (sonda `out/tmp/expected-probe.cpp`, m68k `-O2`): una función
/// que puede fallar cuesta +5 instrucciones frente a `bool`+out-param (23 vs 18) —
/// escribir el error y el flag; el **consumidor** (`r ? r.value() : fallback`) cuesta
/// lo mismo que el `bool` (14/14). **Sin `jsr` ni libcalls** (`__mulsi3` etc. no
/// aparecen). `sizeof(Expected<u16,Err>)` = 4 B (una word con padding).
///
/// Regla de uso: `Expected` en la **frontera pública** y en fallos de `init`/carga
/// (no en hot path); en el camino por frame, donde el `bool` se comprueba miles de
/// veces, `bool`+out-param ya es óptimo. Para `T` grande, ojo: `sizeof` incluye el
/// valor completo (usar salida por puntero si es voluminoso).

#include <eng/core/util/optional.hpp>
#include <eng/core/util/type_traits.hpp>
#include <eng/core/util/util.hpp>

namespace eng::util {

/// Envoltorio del error para construir un `Expected` fallido sin ambigüedad.
template <class E>
struct Unexpected {
	E error;
};

/// Construye el error de un `Expected` (deduce el tipo).
template <class E>
[[nodiscard]] constexpr Unexpected<remove_cvref_t<E>> unexpected(E&& error) {
	return Unexpected<remove_cvref_t<E>> {forward<E>(error)};
}

template <class T, class E>
class Expected {
public:
	using value_type = T;
	using error_type = E;

	static_assert(!is_same_v<T, void>, "Expected<void, E> tiene su propia especialización");

	constexpr Expected(const T& value) : m_value(value), m_ok(true) {}
	constexpr Expected(T&& value) : m_value(move(value)), m_ok(true) {}

	template <class U>
	constexpr Expected(Unexpected<U> err) : m_error(move(err.error)), m_ok(false) {}

	[[nodiscard]] constexpr bool has_value() const noexcept { return m_ok; }
	[[nodiscard]] constexpr explicit operator bool() const noexcept { return m_ok; }

	[[nodiscard]] constexpr T& value() noexcept {
		if (!m_ok) {
			detail::bad_access();
		}
		return m_value;
	}
	[[nodiscard]] constexpr const T& value() const noexcept {
		if (!m_ok) {
			detail::bad_access();
		}
		return m_value;
	}

	[[nodiscard]] constexpr E& error() noexcept {
		if (m_ok) {
			detail::bad_access();
		}
		return m_error;
	}
	[[nodiscard]] constexpr const E& error() const noexcept {
		if (m_ok) {
			detail::bad_access();
		}
		return m_error;
	}

	[[nodiscard]] constexpr T& operator*() noexcept { return value(); }
	[[nodiscard]] constexpr const T& operator*() const noexcept { return value(); }
	[[nodiscard]] constexpr T* operator->() noexcept { return &value(); }
	[[nodiscard]] constexpr const T* operator->() const noexcept { return &value(); }

	/// Valor si lo hay; si no, `fallback`.
	[[nodiscard]] constexpr T value_or(const T& fallback) const {
		return m_ok ? m_value : fallback;
	}

	template <class... Args>
	constexpr T& emplace(Args&&... args) {
		m_value = T(forward<Args>(args)...);
		m_ok = true;
		return m_value;
	}

private:
	T m_value {};
	E m_error {};
	bool m_ok = false;
};

/// `Expected` de una operación que no devuelve valor: solo éxito o error.
template <class E>
class Expected<void, E> {
public:
	using error_type = E;

	/// Éxito.
	constexpr Expected() noexcept : m_ok(true) {}

	template <class U>
	constexpr Expected(Unexpected<U> err) : m_error(move(err.error)), m_ok(false) {}

	[[nodiscard]] constexpr bool has_value() const noexcept { return m_ok; }
	[[nodiscard]] constexpr explicit operator bool() const noexcept { return m_ok; }

	constexpr void value() const noexcept {
		if (!m_ok) {
			detail::bad_access();
		}
	}

	[[nodiscard]] constexpr E& error() noexcept {
		if (m_ok) {
			detail::bad_access();
		}
		return m_error;
	}
	[[nodiscard]] constexpr const E& error() const noexcept {
		if (m_ok) {
			detail::bad_access();
		}
		return m_error;
	}

private:
	E m_error {};
	bool m_ok = true;
};

} // namespace eng::util
