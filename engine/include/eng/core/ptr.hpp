#pragma once

/// \file ptr.hpp
/// **Punteros "inteligentes" sin heap** para el engine: sustituyen al `T*` crudo en las APIs
/// (no propietarios, anulables) y dan un opcional en sitio. No asignan memoria, no usan STL,
/// no usan excepciones; son envoltorios de coste cero pensados para el 68000.
///
/// - `Ref<T>`: **observador no propietario** y anulable. La forma limpia de decir "esto usa
///   este objeto, pero no es su dueño" (p. ej. `Surface` sobre `Playfield`) sin exponer `*`.
/// - `Opt<T>`: **opcional en sitio** (lo que `std::optional` haría, pero sin STL): o no hay
///   valor, o hay un `T` construido dentro.
/// - `NonNull<T>`: como `Ref<T>` pero se construye sólo desde una referencia válida; útil
///   cuando "no puede ser nulo" es parte del contrato.
///
/// No son propietarios: no borran nada (`delete` no existe aquí). Para propiedad real, el
/// engine usa arenas (`Block<T>`, `MemorySystem`).

#include <eng/core/types.hpp>

namespace eng {

/// Referencia no propietaria y anulable. Construcción **implícita** desde `T&`, `T*` o
/// `nullptr`, de modo que en las llamadas se pasa el objeto (o `&objeto`, o `nullptr`) sin
/// escribir el tipo: `f(t, table, range)` o `f(t, nullptr)`. El puntero crudo es anulable por
/// contrato, así que la conversión no añade riesgo.
template <class T>
class Ref {
public:
	constexpr Ref() = default;
	constexpr Ref(T& ref) : m_ptr(&ref) {} // implícita desde referencia: es válida
	/// Implícitas desde `T*`/`nullptr`: el llamador pasa `&objeto` o `nullptr` y el `Ref`
	/// se construye solo, sin escribir el tipo (el puntero crudo es anulable por contrato).
	constexpr Ref(T* ptr) : m_ptr(ptr) {}
	constexpr Ref(decltype(nullptr)) : m_ptr(nullptr) {}

	/// Conversión cualificante `Ref<U> -> Ref<T>` cuando `U*` convierte a `T*`
	/// (típicamente `Ref<T> -> Ref<const T>`). El `requires` evita la conversión a sí misma.
	template <typename U>
		requires requires (U* q) { static_cast<T*>(q); } && (!detail::same<U, T>::value)
	constexpr Ref(const Ref<U>& other) : m_ptr(other.get()) {}

	[[nodiscard]] constexpr bool valid() const { return m_ptr != nullptr; }
	explicit constexpr operator bool() const { return m_ptr != nullptr; }

	[[nodiscard]] constexpr T& operator*() const { return *m_ptr; }
	[[nodiscard]] constexpr T* operator->() const { return m_ptr; }
	[[nodiscard]] constexpr T* get() const { return m_ptr; }
	constexpr void reset() { m_ptr = nullptr; }

private:
	T* m_ptr = nullptr;
};

template <class T>
[[nodiscard]] constexpr bool operator==(Ref<T> a, Ref<T> b) {
	return a.get() == b.get();
}
template <class T>
[[nodiscard]] constexpr bool operator!=(Ref<T> a, Ref<T> b) {
	return a.get() != b.get();
}

/// Observador que **no puede ser nulo** una vez construido (contrato explícito).
template <class T>
class NonNull {
public:
	explicit constexpr NonNull(T& ref) : m_ptr(&ref) {}
	[[nodiscard]] constexpr T& operator*() const { return *m_ptr; }
	[[nodiscard]] constexpr T* operator->() const { return m_ptr; }
	[[nodiscard]] constexpr T* get() const { return m_ptr; }

private:
	T* m_ptr;
};

/// Opcional en sitio (sin heap): `T` vive dentro; sólo es válido si `has_value()`.
template <class T>
class Opt {
public:
	constexpr Opt() = default;
	constexpr Opt(const T& value) : m_value(value), m_has(true) {}

	[[nodiscard]] constexpr bool has_value() const { return m_has; }
	explicit constexpr operator bool() const { return m_has; }

	constexpr T& value() { return m_value; }
	[[nodiscard]] constexpr const T& value() const { return m_value; }
	constexpr T& operator*() { return m_value; }
	[[nodiscard]] constexpr const T& operator*() const { return m_value; }

	constexpr void set(const T& value) {
		m_value = value;
		m_has = true;
	}
	constexpr void reset() { m_has = false; }

private:
	T m_value {};
	bool m_has = false;
};

} // namespace eng
