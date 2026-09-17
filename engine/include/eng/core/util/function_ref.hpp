#pragma once

/// \file function_ref.hpp
/// `eng::util::FunctionRef<Sig>`: referencia no propietaria a un callable
/// (alternativa ligera a `std::function`, sin reserva dinámica).
///
/// El engine evita `std::function` (heap + virtual + copia del cierre). Cuando solo
/// hace falta **pasar** un callable a una función de orden superior (un visitante,
/// una política, un callback de evento que se consume en la misma llamada),
/// `FunctionRef` guarda un puntero al callable y un puntero a un thunk: dos punteros,
/// sin copiar ni asignar.
///
/// Contrato de vida: **no posee** el callable; este debe vivir más que la
/// `FunctionRef`. No construir desde un temporal que muera antes de usar la
/// referencia. Solo admite callables con `operator()` **const** (una lambda sin
/// `mutable` sí; una con `mutable` no).
///
/// Uso:
///   void visit(const eng::util::FunctionRef<void(Actor&)>& fn);
///   visit([](Actor& a) { a.step(); });

#include <eng/core/util/type_traits.hpp>

namespace eng::util {

template <class Sig>
class FunctionRef;

template <class R, class... Args>
class FunctionRef<R(Args...)> {
public:
	constexpr FunctionRef() noexcept = default;

	/// Enlaza `fn` por referencia (no lo copia). `F` no puede ser otra `FunctionRef`.
	template <class F, enable_if_t<!is_same_v<remove_cvref_t<F>, FunctionRef>, int> = 0>
	constexpr FunctionRef(F&& fn) noexcept
		: m_obj(reinterpret_cast<const void*>(&fn)), m_fn(&thunk<remove_cvref_t<F>>) {}

	[[nodiscard]] constexpr R operator()(Args... args) const {
		return m_fn(m_obj, static_cast<Args>(args)...);
	}

private:
	/// Puente por tipo: un objeto-functor se invoca por su dirección; una función
	/// libre guarda la dirección de la función (su "objeto" es la propia función).
	template <class F>
	static R thunk(const void* obj, Args... args) {
		if constexpr (is_function_v<F>) {
			return reinterpret_cast<F*>(const_cast<void*>(obj))(static_cast<Args>(args)...);
		} else {
			return (*static_cast<const F*>(obj))(static_cast<Args>(args)...);
		}
	}

	const void* m_obj = nullptr;
	R (*m_fn)(const void*, Args...) = nullptr;
};

} // namespace eng::util
