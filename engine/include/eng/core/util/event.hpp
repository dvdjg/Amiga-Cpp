#pragma once

/// \file event.hpp
/// `eng::util::Event<Signature, MaxSubscribers>`: **emisor de eventos** de capacidad
/// fija (patrón observador) sin heap ni virtuals. Guarda una lista de suscriptores
/// `FunctionRef` (referencias no propietarias) y los invoca en orden de suscripción al
/// hacer `emit`.
///
/// Contrato de vida: **no posee** los callables; deben vivir más que el evento (mismo
/// contrato que `FunctionRef`). Pensado para eventos de juego (daño, colisión, cambio de
/// fase, órdenes) y para la difusión del blackboard de IA, siempre con un número de
/// suscriptores conocido de antemano.
///
/// Uso:
///   eng::util::Event<void(int), 4> on_damage;
///   on_damage.subscribe([](int hp) { ... });   // el callable debe seguir vivo
///   on_damage.emit(10);
///
/// Verificación: HOST-109.

#include <eng/core/types.hpp>
#include <eng/core/util/function_ref.hpp>

namespace eng::util {

template <class Signature, usize MaxSubscribers>
class Event;

template <class... Args, usize MaxSubscribers>
class Event<void(Args...), MaxSubscribers> {
public:
	using Handler = FunctionRef<void(Args...)>;

	[[nodiscard]] static constexpr usize capacity() noexcept { return MaxSubscribers; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_count; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_count == 0u; }

	/// Suscribe `handler` (no lo copia ni lo posee). `false` si está lleno.
	constexpr bool subscribe(Handler handler) noexcept {
		if (m_count >= MaxSubscribers) {
			return false;
		}
		m_handlers[m_count] = handler;
		++m_count;
		return true;
	}

	/// Invoca a todos los suscriptores, en orden de suscripción.
	constexpr void emit(Args... args) const {
		for (usize i = 0; i < m_count; ++i) {
			m_handlers[i](args...);
		}
	}

	/// Da de baja a todos los suscriptores.
	constexpr void clear() noexcept { m_count = 0u; }

private:
	Handler m_handlers[MaxSubscribers] {};
	usize m_count = 0u;
};

} // namespace eng::util
