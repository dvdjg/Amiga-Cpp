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
#include <eng/ai/planning/htn.hpp>
#include <eng/ai/planning/numeric_goap.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/personality.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Alias del dominio GOAP del ecosistema (32 hechos, el caso normal y mas ligero).
using SimGoap = eng::ai::Goap<>;

/// Dominio GOAP **numerico** del ecosistema: hechos + `MaxVars` magnitudes (hambre,
/// energia, miedo...). Se instancian solo las variables usadas; elige el dominio pasandolo
/// como parametro de plantilla al `PlannerDriver`. Ver `GOAP_EXTENDED.md` y HOST-313.
template <eng::u8 MaxVars = 4u>
using SimNumericGoap = eng::ai::NumericGoap<MaxVars>;

/// ¿Cuándo merece la pena planificar? Parámetros de diseño. Los umbrales de **entrada**
/// (`min_*`) y de **salida** (`exit_*`, menores) forman la banda de histéresis: una criatura
/// empieza a planificar al superar los de entrada y no deja de hacerlo hasta bajar de los de
/// salida, de modo que no parpadea si ronda el umbral.
struct PlanParams {
	eng::u8 min_curiosity = 60u;  ///< curiosidad para **empezar** a planificar
	eng::u8 min_autonomy = 40u;   ///< autonomía para empezar
	eng::u8 exit_curiosity = 40u; ///< curiosidad por debajo de la cual se deja de planificar
	eng::u8 exit_autonomy = 25u;  ///< autonomía por debajo de la cual se deja de planificar
	eng::u16 replan_interval = 120u; ///< ticks mínimos entre replanificaciones
	eng::u16 budget = 0u; ///< presupuesto de expansiones por búsqueda (0 = sin límite);
			      ///< el juego lo aplica al `PlannerDriver` con `set_budget`
};

/// ¿Toca planificar en `frame_now`? Aplica la **histéresis** (entra con `min_*`, se mantiene
/// hasta bajar de `exit_*`) y respeta `replan_interval` entre replanes. Actualiza `st`.
[[nodiscard]] constexpr bool should_replan(const Personality& p, PlanState& st, eng::u16 frame_now,
					   const PlanParams& params = PlanParams {}) noexcept {
	if (p.curiosity >= params.min_curiosity && p.autonomy >= params.min_autonomy) {
		st.active = true;
	} else if (p.curiosity <= params.exit_curiosity ||
		   p.autonomy <= params.exit_autonomy) {
		st.active = false;
	}
	if (!st.active) {
		return false;
	}
	if (st.planned &&
	    static_cast<eng::u16>(frame_now - st.last) < params.replan_interval) {
		return false;
	}
	st.planned = true;
	st.last = frame_now;
	return true;
}



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

/// Conductor: planifica con el dominio `DomainT` y ejecuta el plan por ticks. El dominio
/// por defecto es el booleano ligero (`SimGoap`, 0 variables); pasa `SimNumericGoap<N>` —u
/// otro tipo con `State`/`Goal`/`Action`/`Planner`— para planificar magnitudes.
template <eng::u16 MaxNodes = 96u, eng::u8 MaxSteps = kMaxPlanSteps, class DomainT = SimGoap>
class PlannerDriver {
public:
	using Ai = DomainT;

	/// Planifica desde `start` hacia `goal`. Devuelve `true` si hay plan (o el objetivo ya
	/// se cumple). Escribe los pasos en el `PlanRunner`. Usa la **caché de planes**
	/// (`plan_cached`): replantear el mismo objetivo desde el mismo estado no vuelve a
	/// buscar (clave en el bucle del sim, donde muchas criaturas repiten consulta). Con
	/// presupuesto (`set_budget`), puede devolver un plan **parcial** (`partial()`).
	[[nodiscard]] constexpr bool replan(const typename Ai::State& start,
					    const typename Ai::Goal& goal,
					    eng::Span<const typename Ai::Action> actions) noexcept {
		eng::u16 raw[MaxSteps] {};
		const eng::usize n = m_planner.plan_cached(start, goal, actions,
							   {raw, MaxSteps});
		if (!m_planner.found() && n == 0u) {
			m_runner.abort();
			return false;
		}
		// `n == 0` con `found()`: el estado ya cumple el objetivo (plan vacío).
		return m_runner.load(eng::Span<const eng::u16> {raw, n});
	}

	/// Tras ejecutar pasos del plan en curso, **reutiliza el sufijo**: si el resto sigue
	/// siendo aplicable desde `start` y cumple el objetivo, lo devuelve sin buscar; si no,
	/// replantea (caché + búsqueda). Evita repetir la búsqueda cuando la criatura solo ha
	/// avanzado un paso. Ver HOST-155.
	[[nodiscard]] constexpr bool replan_reusing(const typename Ai::State& start,
						    const typename Ai::Goal& goal,
						    eng::Span<const typename Ai::Action> actions) noexcept {
		if (m_runner.cursor == 0u) {
			return replan(start, goal, actions); // nada ejecutado aun: plan normal
		}
		eng::u16 raw[MaxSteps] {};
		const eng::usize taken = static_cast<eng::usize>(m_runner.cursor - 1u);
		const eng::usize n = m_planner.plan_reusing(
		    start, goal, actions, eng::Span<eng::u16> {raw, MaxSteps},
		    eng::Span<const eng::u16> {m_runner.steps, m_runner.count}, taken);
		if (!m_planner.found() && n == 0u) {
			m_runner.abort();
			return false;
		}
		return m_runner.load(eng::Span<const eng::u16> {raw, n});
	}

	[[nodiscard]] constexpr bool has_plan() const noexcept { return m_runner.active; }
	[[nodiscard]] constexpr bool done() const noexcept { return m_runner.done(); }
	[[nodiscard]] constexpr eng::u16 current() const noexcept { return m_runner.current(); }
	constexpr void advance() noexcept { m_runner.advance(); }
	constexpr void abort() noexcept { m_runner.abort(); }

	/// Presupuesto *anytime* de la búsqueda (0 = sin límite). Con presupuesto, `replan`
	/// puede dejar un plan **parcial** (`partial()`), con menos pasos que el óptimo; el
	/// llamador decide si lo ejecuta o espera más presupuesto. Ver HOST-313.
	constexpr void set_budget(eng::usize max_expansions) noexcept {
		m_planner.set_budget(max_expansions);
	}
	[[nodiscard]] constexpr bool partial() const noexcept { return m_planner.partial(); }

	[[nodiscard]] constexpr eng::usize expansions() const noexcept {
		return m_planner.expansions();
	}

	/// Vacía la caché de planes (llamar si cambia el dominio de acciones).
	constexpr void clear_plan_cache() noexcept { m_planner.clear_plan_cache(); }
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

/// Aplica el presupuesto de `PlanParams` al conductor (0 = sin límite). Se llama una vez por
/// criatura/tick, antes de `replan`. Ver HOST-155.
template <eng::u16 MaxNodes, eng::u8 MaxSteps, class DomainT>
constexpr void apply_budget(PlannerDriver<MaxNodes, MaxSteps, DomainT>& driver,
			    const PlanParams& params) noexcept {
	driver.set_budget(params.budget);
}

/// Conductor **HTN**: guarda una red de tareas (`eng::ai::Htn`) y planifica por
/// **descomposición** (sin A\*), cargando el resultado en un `PlanRunner` — igual que
/// `PlannerDriver` con GOAP. La criatura planifica así cuando el objetivo es una tarea
/// compuesta; el mundo consume el plan con `store_plan`. Ver HOST-313.
template <usize MaxFacts, usize MaxActions, usize MaxCompounds, usize MaxMethods,
	  usize MaxSubtasks, eng::u8 MaxSteps = kMaxPlanSteps>
class HtnDriver {
public:
	using Domain = eng::ai::Htn<MaxFacts, MaxActions, MaxCompounds, MaxMethods, MaxSubtasks>;
	using State = typename Domain::State;
	using Action = typename Domain::Action;

	/// Fija la red de tareas (una vez, al arrancar el juego).
	constexpr void set_domain(const Domain& domain_in) noexcept { m_domain = domain_in; }
	[[nodiscard]] constexpr const Domain& domain() const noexcept { return m_domain; }

	/// Descompone `root` desde `start` y carga el plan en el runner. Devuelve `true` si hay
	/// plan (una descomposición de longitud 0 no se considera plan).
	[[nodiscard]] constexpr bool replan(const State& start, const eng::ai::HtnCompound& root,
					    eng::Span<const Action> actions) noexcept {
		eng::u16 raw[MaxSteps] {};
		const eng::usize n =
		    m_domain.plan(start, root, actions, eng::Span<eng::u16> {raw, MaxSteps});
		if (n == 0u) {
			m_runner.abort();
			return false;
		}
		return m_runner.load(eng::Span<const eng::u16> {raw, n});
	}

	[[nodiscard]] constexpr bool has_plan() const noexcept { return m_runner.active; }
	[[nodiscard]] constexpr bool done() const noexcept { return m_runner.done(); }
	[[nodiscard]] constexpr eng::u16 current() const noexcept { return m_runner.current(); }
	constexpr void advance() noexcept { m_runner.advance(); }
	constexpr void abort() noexcept { m_runner.abort(); }
	[[nodiscard]] constexpr PlanRunner<MaxSteps>& runner() noexcept { return m_runner; }
	[[nodiscard]] constexpr const PlanRunner<MaxSteps>& runner() const noexcept {
		return m_runner;
	}

private:
	Domain m_domain {};
	PlanRunner<MaxSteps> m_runner {};
};

/// Tipo de objetivo: un **objetivo suelto** lo resuelve el GOAP (búsqueda A\*); una **tarea
/// compuesta** la descompone el HTN. El juego lo declara con este enum y no repite el `if`.
enum class PlanKind : eng::u8 {
	Goap = 0u,
	Htn = 1u,
};

/// Ejecuta el planificador adecuado para `kind` y deja el plan asociado a `id`: con `Goap`
/// va el bucle del mundo (`plan_tick`: decisión + presupuesto + replan); con `Htn`,
/// descompone `root` con el conductor HTN y guarda el resultado (`store_plan`). Así el juego
/// solo declara el **tipo de objetivo**. Ver HOST-313.
template <class WorldT, class HtnDriverT>
[[nodiscard]] constexpr bool plan_for(WorldT& world, HtnDriverT& htn_driver, PlanKind kind,
				      EntityId id, const typename WorldT::Ai::State& start,
				      const typename WorldT::Ai::Goal& goal,
				      const eng::ai::HtnCompound& root,
				      eng::Span<const typename WorldT::Ai::Action> actions,
				      eng::u16 frame_now,
				      const PlanParams& params = PlanParams {}) noexcept {
	if (kind == PlanKind::Htn) {
		if (!htn_driver.replan(start, root, actions)) {
			return false;
		}
		return world.store_plan(id, htn_driver.runner());
	}
	return world.plan_tick(id, start, goal, actions, frame_now, params);
}

} // namespace eng::sim
