#pragma once

/// \file result.hpp
/// **Valor o error** (`eng::Expected<T>`): el análogo de `std::expected<T, E>` del C++23 para el
/// engine, sin excepciones ni heap. Es el **idioma único de error** de las APIs nuevas: en vez
/// de `bool` + out-param, `0`, bloque inválido o `Ref` nulo, una función devuelve el **valor** o
/// un **`eng::Result`** (el código de estado de `types.hpp`).
///
/// ```cpp
/// eng::Expected<eng::u16> load(const char* path);   // id o error
/// auto r = load("data/x");
/// if (r) { use(*r); } else { log(r.status()); }
/// ```
///
/// `Expected<T>` envuelve `Opt<T>` (opcional en sitio) y se construye implícitamente desde `T`
/// o desde un `Result` de error. Un `Result::Ok` como error no es válido (sería "ok sin valor").

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>

namespace eng {

template <class T>
class Expected {
public:
	constexpr Expected() noexcept = default;
	/// Éxito con valor.
	constexpr Expected(const T& value) noexcept : m_value(value), m_status(Result::Ok) {}
	/// Error (no debe ser `Result::Ok`).
	constexpr Expected(Result error) noexcept : m_status(error) {}

	[[nodiscard]] constexpr bool ok() const noexcept { return m_status == Result::Ok; }
	explicit constexpr operator bool() const noexcept { return ok(); }
	/// Código de estado (`Result::Ok` si hay valor).
	[[nodiscard]] constexpr Result status() const noexcept { return m_status; }

	/// Valor (solo válido si `ok()`).
	[[nodiscard]] constexpr const T& value() const noexcept { return m_value.value(); }
	constexpr T& value() noexcept { return m_value.value(); }
	[[nodiscard]] constexpr const T& operator*() const noexcept { return m_value.value(); }
	constexpr T& operator*() noexcept { return m_value.value(); }
	/// Valor o `fallback` si hay error.
	[[nodiscard]] constexpr T value_or(const T& fallback) const noexcept {
		return ok() ? m_value.value() : fallback;
	}

private:
	Opt<T> m_value {};
	Result m_status = Result::Ok;
};

} // namespace eng
