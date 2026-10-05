#pragma once

/// \file callback.hpp
/// `eng::util::Callback<Args...>`: **callback no propietario y POD** del engine
/// (puntero a función + contexto). Sustituye el patrón escrito a mano
///
///   void (*fn)(void* user);
///   void* user;
///
/// por un único miembro de dos palabras que se invoca con `cb(args...)`. A diferencia
/// de `FunctionRef`, es un agregado **trivialmente copiable y C-ABI-seguro**: válido
/// para cruzar una frontera que exige POD (un registro de hardware, un hook de IRQ,
/// una tabla plana).
///
/// Es la forma de un callback **con estado** sin heap y sin `std::function`: el estado
/// va en `ctx` (lo aporta el llamador) y la función recibe `(void* ctx, Args...)`. Para
/// un callable sin contexto, `FunctionRef` (`function_ref.hpp`) es más expresivo; aquí
/// el contexto es explícito y el objeto cabe en dos palabras. El `ctx` **no se posee**:
/// debe sobrevivir a las llamadas.
///
/// Uso:
///   void on_click(void* ctx, eng::s16 x, eng::s16 y) { ... }
///   eng::util::Callback<eng::s16, eng::s16> cb { &on_click, &state };
///   if (cb) { cb(3, 4); }
///
/// Verificación: HOST-079.

#include <eng/core/types/types.hpp>

namespace eng::util {

template <class... Args>
struct Callback {
	using fn_type = void (*)(void*, Args...);

	fn_type fn = nullptr;
	void* ctx = nullptr;

	/// ¿Hay callback instalado?
	[[nodiscard]] constexpr bool valid() const noexcept { return fn != nullptr; }

	/// `true` si hay callback (permite `if (cb) cb(...)`).
	explicit constexpr operator bool() const noexcept { return fn != nullptr; }

	/// Invoca el callback con el contexto guardado (no-op si no hay).
	void operator()(Args... args) const {
		if (fn != nullptr) {
			fn(ctx, args...);
		}
	}

	/// Desinstala el callback (deja de invocarse).
	constexpr void clear() noexcept {
		fn = nullptr;
		ctx = nullptr;
	}
};

} // namespace eng::util
