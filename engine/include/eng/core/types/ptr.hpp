#pragma once

/// \file ptr.hpp
/// **Punteros "inteligentes" sin heap** para el engine: sustituyen al `T*` crudo en las APIs
/// (no propietarios, anulables). No asignan memoria, no usan STL, no usan excepciones; son
/// envoltorios de coste cero pensados para el 68000.
///
/// - `Ref<T>`: **observador no propietario** y anulable. La forma limpia de decir "esto usa
///   este objeto, pero no es su dueño" (p. ej. `Surface` sobre `Playfield`) sin exponer `*`.
/// - `NonNull<T>`: como `Ref<T>` pero se construye sólo desde una referencia válida; útil
///   cuando "no puede ser nulo" es parte del contrato.
///
/// (El opcional en sitio es `eng::util::Optional<T>` y el valor-o-error `eng::util::Expected<T,E>`,
/// en `core/util/`; no se duplican aquí.)
///
/// No son propietarios: no borran nada (`delete` no existe aquí). Para propiedad real, el
/// engine usa arenas (`Block<T>`, `MemorySystem`).

#include <eng/core/types/types.hpp>

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

} // namespace eng
