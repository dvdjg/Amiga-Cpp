#pragma once

/// \file util.hpp
/// Utilidades de lenguaje del engine (`eng::util`), sin `<utility>`.
///
/// Reúne las operaciones que C++ moderno da por hechas (`std::move`,
/// `std::forward`, `std::swap`, `std::exchange`, `std::as_const`,
/// `std::min`/`std::max`/`std::clamp`) y que el runtime freestanding no tiene.
/// Son la base de los contenedores y algoritmos de `eng/core/util/`.
///
/// Nota de resolución de nombres: `eng::math` ya ofrece `min`/`max`/`clamp`
/// **para escalares** (funcionan con `Fixed`/`MiniFloat16` y devuelven por
/// valor). Estas versiones son **genéricas** (cualquier tipo con `operator<`,
/// devuelven referencia) y viven en `eng::util`; no se mezclan salvo que se
/// importen los dos espacios de nombres a la vez.
///
/// Uso:
///   auto tmp = eng::util::exchange(state, next);
///   eng::util::swap(a, b);
///   const auto& lo = eng::util::clamp(v, min_v, max_v);

#include <eng/core/util/type_traits.hpp>

namespace eng::util {

/// Convierte a referencia rvalue (equivalente a `std::move`). Con `T` deducido de
/// una referencia, `remove_reference_t<T>&&` es la referencia correcta y el
/// valor no se copia.
template <class T>
[[nodiscard]] constexpr remove_reference_t<T>&& move(T&& value) noexcept {
	return static_cast<remove_reference_t<T>&&>(value);
}

/// Reenvío perfecto (equivalente a `std::forward`).
template <class T>
[[nodiscard]] constexpr T&& forward(remove_reference_t<T>& value) noexcept {
	return static_cast<T&&>(value);
}
template <class T>
[[nodiscard]] constexpr T&& forward(remove_reference_t<T>&& value) noexcept {
	static_assert(!is_lvalue_reference_v<T>, "forward: no se puede reenviar un rvalue como lvalue");
	return static_cast<T&&>(value);
}

/// Intercambia dos valores del mismo tipo (tres movimientos).
template <class T>
constexpr void swap(T& a, T& b) noexcept {
	T tmp = move(a);
	a = move(b);
	b = move(tmp);
}

/// Intercambio de arrays del mismo tipo y tamaño, elemento a elemento.
template <class T, usize N>
constexpr void swap(T (&a)[N], T (&b)[N]) noexcept {
	for (usize i = 0; i < N; ++i) {
		swap(a[i], b[i]);
	}
}

/// Guarda el valor actual de `obj` en `new_value` y devuelve el anterior
/// (patrón de los dobles buffers y de los contadores que se reinician).
template <class T, class U = T>
[[nodiscard]] constexpr T exchange(T& obj, U&& new_value) noexcept {
	T old = move(obj);
	obj = forward<U>(new_value);
	return old;
}

/// Vista de solo lectura de un valor (evita deducir una copia).
template <class T>
[[nodiscard]] constexpr const T& as_const(T& value) noexcept {
	return value;
}

/// Menor de dos valores (referencia, sin copia).
template <class T>
[[nodiscard]] constexpr const T& min(const T& a, const T& b) noexcept {
	return b < a ? b : a;
}

/// Mayor de dos valores (referencia, sin copia).
template <class T>
[[nodiscard]] constexpr const T& max(const T& a, const T& b) noexcept {
	return a < b ? b : a;
}

/// Recorta `v` a `[lo, hi]` (referencia, sin copia). `lo <= hi` es contrato.
template <class T>
[[nodiscard]] constexpr const T& clamp(const T& v, const T& lo, const T& hi) noexcept {
	return v < lo ? lo : (hi < v ? hi : v);
}

} // namespace eng::util
