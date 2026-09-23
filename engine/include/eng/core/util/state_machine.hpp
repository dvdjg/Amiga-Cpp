#pragma once

/// \file state_machine.hpp
/// `eng::util::StateMachine<State, Event>`: **maquina de estados finita** con tabla de
/// transiciones `constexpr` externa (sin heap, sin virtuals). El coste de `dispatch` es
/// `O(transiciones)` con una comparacion entera por entrada, apto para el 68000.
///
/// La maquina **no posee la tabla**: guarda una `Span<const Transition<State, Event>>`
/// que debe seguir viva (tipicamente un array `constexpr` global). Busca la **primera**
/// transicion cuyo `(estado actual, evento)` coincide y cambia de estado; si no hay
/// ninguna, no hace nada y devuelve `false`.
///
/// Es el motor generico de decision del engine (menus, flujo de juego, animacion) y la
/// base de `eng::ai::decision`: la capa de IA aporta los estados/eventos y las acciones
/// (efectos al entrar/salir), no una segunda FSM. Ver
/// `docs/engine/architecture/GAME_AI_LIBRARY.md`.
///
/// Uso:
///   enum class Luz { Rojo, Verde, Ambar };
///   enum class Tick { Paso };
///   constexpr eng::util::Transition<Luz, Tick> tabla[] = {
///       {Luz::Rojo,  Tick::Paso, Luz::Verde},
///       {Luz::Verde, Tick::Paso, Luz::Ambar},
///       {Luz::Ambar, Tick::Paso, Luz::Rojo},
///   };
///   eng::util::StateMachine<Luz, Tick> semaforo { Luz::Rojo, tabla };
///   semaforo.dispatch(Tick::Paso);   // -> Verde
///
/// Verificacion: HOST-108.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::util {

/// Una transicion `from --event--> to` de la maquina.
template <class State, class Event>
struct Transition {
	State from;
	Event event;
	State to;
};

template <class State, class Event>
class StateMachine {
public:
	using TransitionT = Transition<State, Event>;

	constexpr StateMachine(State initial, Span<const TransitionT> table) noexcept
		: m_current(initial), m_initial(initial), m_table(table) {}

	[[nodiscard]] constexpr State current() const noexcept { return m_current; }
	[[nodiscard]] constexpr bool is(State s) const noexcept { return m_current == s; }
	[[nodiscard]] constexpr usize table_size() const noexcept { return m_table.size(); }
	[[nodiscard]] constexpr usize transition_count() const noexcept { return m_count; }

	/// Aplica `event`: si hay una transicion desde el estado actual, cambia y devuelve
	/// `true`; si no, mantiene el estado y devuelve `false`.
	constexpr bool dispatch(Event event) noexcept {
		for (usize i = 0; i < m_table.size(); ++i) {
			const TransitionT& t = m_table[i];
			if (t.from == m_current && t.event == event) {
				m_current = t.to;
				++m_count;
				return true;
			}
		}
		return false;
	}

	/// Vuelve al estado inicial (y reinicia el contador de transiciones).
	constexpr void reset() noexcept {
		m_current = m_initial;
		m_count = 0u;
	}

	/// Fija `state` como nuevo estado actual e inicial (reinicia el contador).
	constexpr void reset(State state) noexcept {
		m_initial = state;
		m_current = state;
		m_count = 0u;
	}

private:
	State m_current;
	State m_initial;
	Span<const TransitionT> m_table;
	usize m_count = 0u;
};

} // namespace eng::util
