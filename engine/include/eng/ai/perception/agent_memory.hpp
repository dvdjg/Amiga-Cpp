#pragma once

/// \file agent_memory.hpp
/// `eng::ai::AgentMemory`: **memoria del agente** sobre lo último que vio (posición
/// conocida del objetivo y cuántos ticks hace). El agente decide con información
/// **caducada pero útil** ("irá hacia donde lo vi"), en vez de olvidar al instante. La
/// política (cuánto dura la memoria) la fija el juego con `fresh`/`stale`.
///
/// Uso:
///   eng::ai::AgentMemory mem;
///   mem.see({120, 40});        // avistamiento
///   mem.tick();                // cada frame del juego
///   if (mem.fresh(30)) { /* perseguir mem.last_position */ }
///
/// Verificación: HOST-117.

#include <eng/core/types/types.hpp>

namespace eng::ai {

struct AgentMemory {
	eng::Point2s last_position {};
	eng::u16 ticks_since_seen = 0u;
	bool has_target = false;

	/// Registra un avistamiento: fija la posición y reinicia el contador.
	constexpr void see(eng::Point2s position) noexcept {
		last_position = position;
		ticks_since_seen = 0u;
		has_target = true;
	}

	/// Olvida al objetivo.
	constexpr void forget() noexcept {
		has_target = false;
		ticks_since_seen = 0u;
	}

	/// Avanza un tick de juego; satura el contador si no hay avistamiento.
	constexpr void tick() noexcept {
		if (has_target && ticks_since_seen != 0xffffu) {
			++ticks_since_seen;
		}
	}

	/// ¿La memoria es reciente (vista hace <= `max_age` ticks)?
	[[nodiscard]] constexpr bool fresh(eng::u16 max_age) const noexcept {
		return has_target && ticks_since_seen <= max_age;
	}

	/// ¿Hay recuerdo pero ya ha caducado (> `max_age` ticks)?
	[[nodiscard]] constexpr bool stale(eng::u16 max_age) const noexcept {
		return has_target && ticks_since_seen > max_age;
	}
};

} // namespace eng::ai
