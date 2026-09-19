#pragma once

/// \file agent_fsm.hpp
/// `eng::ai::AgentFsm<State, Event, MaxStates>`: **FSM de agente** para la capa de
/// decisión de la IA. Añade a `eng::util::StateMachine` (que ya resuelve las
/// transiciones) los **efectos de entrada y salida de estado**, que es donde el agente
/// actúa: cambiar de animación, fijar el objetivo, disparar un sonido, registrar en el
/// blackboard.
///
/// No duplica la máquina de estados: la **contiene**. Los estados deben ser un `enum`
/// denso en `[0, MaxStates)` (se indexan por su valor). Los efectos son `FunctionRef`
/// (no poseen el callable; deben vivir más que la FSM).
///
/// Uso:
///   enum class Guard : eng::u8 { Patrulla, Alerta, Persecucion };
///   enum class Cmd : eng::u8 { VeEnemigo, Pierde };
///   constexpr eng::util::Transition<Guard, Cmd> tabla[] = { ... };
///   eng::ai::AgentFsm<Guard, Cmd, 3> fsm { Guard::Patrulla, tabla };
///   fsm.on_enter(Guard::Alerta, []() { /* ... */ });
///   fsm.dispatch(Cmd::VeEnemigo);   // ejecuta salida y entrada
///
/// Referencia y encaje: `docs/engine/architecture/GAME_AI_LIBRARY.md`.
/// Verificación: HOST-110.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/bitset.hpp>
#include <eng/core/util/function_ref.hpp>
#include <eng/core/util/state_machine.hpp>

namespace eng::ai {

template <class State, class Event, eng::usize MaxStates>
class AgentFsm {
	static_assert(MaxStates > 0u, "AgentFsm: MaxStates debe ser mayor que 0");

public:
	using Fsm = eng::util::StateMachine<State, Event>;
	using TransitionT = eng::util::Transition<State, Event>;
	using Effect = eng::util::FunctionRef<void()>;

	constexpr AgentFsm(State initial, eng::Span<const TransitionT> table) noexcept
		: m_fsm(initial, table), m_previous(initial) {}

	[[nodiscard]] constexpr State current() const noexcept { return m_fsm.current(); }
	[[nodiscard]] constexpr State previous() const noexcept { return m_previous; }
	[[nodiscard]] constexpr bool is(State s) const noexcept { return m_fsm.is(s); }
	/// Número de transiciones de la tabla (diagnóstico del FSM subyacente).
	[[nodiscard]] constexpr eng::usize transition_count() const noexcept {
		return m_fsm.transition_count();
	}

	/// Registra el efecto que corre al **entrar** en `s` (reemplaza al anterior).
	constexpr void on_enter(State s, Effect fn) noexcept {
		const eng::usize i = index(s);
		m_enter[i] = fn;
		m_has_enter.set(i);
	}

	/// Registra el efecto que corre al **salir** de `s` (reemplaza al anterior).
	constexpr void on_exit(State s, Effect fn) noexcept {
		const eng::usize i = index(s);
		m_exit[i] = fn;
		m_has_exit.set(i);
	}

	/// Aplica `event`: si hay transición, ejecuta la salida del estado actual, cambia y
	/// ejecuta la entrada del nuevo. Sin transición no toca efectos y devuelve `false`.
	bool dispatch(Event event) noexcept {
		const State from = m_fsm.current();
		if (!m_fsm.dispatch(event)) {
			return false;
		}
		const State to = m_fsm.current();
		run_exit(from);
		m_previous = from;
		run_enter(to);
		return true;
	}

private:
	/// Índice del slot de efectos del estado `s` (el valor entero del enum `State`).
	[[nodiscard]] static constexpr eng::usize index(State s) noexcept {
		return static_cast<eng::usize>(s);
	}

	/// Ejecuta el efecto de **entrada** de `s` si se registró con `on_enter`. Lo llama `dispatch`.
	void run_enter(State s) noexcept {
		const eng::usize i = index(s);
		if (m_has_enter.test(i)) {
			m_enter[i]();
		}
	}
	/// Ejecuta el efecto de **salida** de `s` si se registró con `on_exit`. Lo llama `dispatch`.
	void run_exit(State s) noexcept {
		const eng::usize i = index(s);
		if (m_has_exit.test(i)) {
			m_exit[i]();
		}
	}

	Fsm m_fsm;
	State m_previous;
	Effect m_enter[MaxStates] {};
	Effect m_exit[MaxStates] {};
	eng::util::BitSet<MaxStates> m_has_enter {};
	eng::util::BitSet<MaxStates> m_has_exit {};
};

} // namespace eng::ai
