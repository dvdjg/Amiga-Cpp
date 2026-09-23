#pragma once

/// \file planner.hpp
/// **Planificación GOAP para criaturas** (`eng::sim`): capa fina que conecta el
/// planificador genérico `eng::ai::Goap` con el modelo de criatura. No reimplementa el
/// algoritmo (A* sobre hechos booleanos, con caché) sino que aporta el **estado de
/// ejecución** de un plan a lo largo de ticks y la **decisión de cuándo planificar**.
///
/// Encaje: la mayoría de criaturas decide por utilidad cada tick (barato); unas pocas
/// —curiosas, autónomas o de especie "inteligente"— planifican de vez en cuando para
/// objetivos que requieren varios pasos (construir, conseguir una herramienta, llegar a
/// un recurso lejano). `PlannerDriver` guarda el plan (`MaxSteps` acciones) y avanza un
/// paso por tick, sin heap y con presupuesto acotado.
///
/// El **dominio** (hechos, acciones, objetivos) es del juego: el engine solo fija cómo
/// lanzar el plan (`replan`), cómo consumirlo (`current`/`advance`) y cuándo repetir
/// (`plans`). El planificador de `Goap` se instancia en memoria estática o de fondo
/// (ocupa miles de bytes), nunca en la pila del 68000.
///
/// Referencia: `docs/engine/architecture/GAME_AI_LIBRARY.md` §3.
/// Verificación: HOST-155.

#include <eng/ai/planning/goap.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/personality.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Alias del dominio GOAP del ecosistema (32 hechos, el caso normal).
using SimGoap = eng::ai::Goap<>;

/// ¿Cuándo merece la pena planificar? Parámetros de diseño.
struct PlanParams {
	eng::u8 min_curiosity = 60u; ///< curiosidad mínima del planificador
	eng::u8 min_autonomy = 40u;  ///< autonomía mínima (independencia)
	eng::u16 replan_interval = 120u; ///< ticks mínimos entre replanificaciones
};

/// ¿La criatura es de las que planifican? (curiosa y con cierta independencia)
[[nodiscard]] constexpr bool plans(const Personality& personality,
				   const PlanParams& p = PlanParams {}) noexcept {
	return personality.curiosity >= p.min_curiosity &&
	       personality.autonomy >= p.min_autonomy;
}

/// Estado de ejecución de un plan GOAP: pasos restantes y paso actual.
template <eng::u8 MaxSteps = kMaxPlanSteps>
struct PlanRunner {
	eng::u16 steps[MaxSteps] {};
	eng::u16 cost = 0;
	eng::u8 count = 0;
	eng::u8 cursor = 0;
	bool active = false;

	/// Carga un plan (índices de acción) y lo activa.
	constexpr bool load(eng::Span<const eng::u16> plan) noexcept {
		if (plan.size() > MaxSteps) {
			abort();
			return false;
		}
		for (eng::usize i = 0; i < plan.size(); ++i) {
			steps[i] = plan[i];
		}
		count = static_cast<eng::u8>(plan.size());
		cursor = 0;
		active = count > 0u;
		return true;
	}

	[[nodiscard]] constexpr bool done() const noexcept { return !active || cursor >= count; }
	[[nodiscard]] constexpr bool has_next() const noexcept { return active && cursor < count; }

	/// Acción actual (o `0xffff` si no hay).
	[[nodiscard]] constexpr eng::u16 current() const noexcept {
		return has_next() ? steps[cursor] : static_cast<eng::u16>(0xffffu);
	}

	/// Consume el paso actual.
	constexpr void advance() noexcept {
		if (cursor < count) {
			++cursor;
		}
		if (cursor >= count) {
			active = false;
		}
	}

	constexpr void abort() noexcept {
		count = 0;
		cursor = 0;
		active = false;
	}
};

/// Conductor: planifica con `Goap` y ejecuta el plan por ticks.
template <eng::u16 MaxNodes = 96u, eng::u8 MaxSteps = kMaxPlanSteps>
class PlannerDriver {
public:
	using Ai = SimGoap;

	/// Planifica desde `start` hacia `goal`. Devuelve `true` si hay plan (o el objetivo ya
	/// se cumple). Escribe los pasos en el `PlanRunner`. Usa producto/cociente nativos de
	/// 16 bits (sin libcalls).
	[[nodiscard]] constexpr bool replan(const typename Ai::State& start,
					    const typename Ai::Goal& goal,
					    eng::Span<const typename Ai::Action> actions) noexcept {
		eng::u16 raw[MaxSteps] {};
		const eng::usize n = m_planner.plan(start, goal, actions,
						    {raw, MaxSteps});
		if (!m_planner.found() && n == 0u) {
			m_runner.abort();
			return false;
		}
		// `n == 0` con `found()`: el estado ya cumple el objetivo (plan vacío).
		return m_runner.load(eng::Span<const eng::u16> {raw, n});
	}

	[[nodiscard]] constexpr bool has_plan() const noexcept { return m_runner.active; }
	[[nodiscard]] constexpr bool done() const noexcept { return m_runner.done(); }
	[[nodiscard]] constexpr eng::u16 current() const noexcept { return m_runner.current(); }
	constexpr void advance() noexcept { m_runner.advance(); }
	constexpr void abort() noexcept { m_runner.abort(); }

	[[nodiscard]] constexpr eng::usize expansions() const noexcept {
		return m_planner.expansions();
	}
	[[nodiscard]] constexpr eng::u16 plan_cost() const noexcept { return m_planner.plan_cost(); }
	[[nodiscard]] constexpr PlanRunner<MaxSteps>& runner() noexcept { return m_runner; }
	[[nodiscard]] constexpr const PlanRunner<MaxSteps>& runner() const noexcept {
		return m_runner;
	}

private:
	typename Ai::Planner<MaxNodes> m_planner {};
	PlanRunner<MaxSteps> m_runner {};

	static_assert(MaxNodes > 0u, "PlannerDriver: MaxNodes > 0");
	static_assert(MaxSteps > 0u, "PlannerDriver: MaxSteps > 0");
};

} // namespace eng::sim
