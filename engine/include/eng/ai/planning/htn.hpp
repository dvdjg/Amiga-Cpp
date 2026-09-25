#pragma once

/// \file htn.hpp
/// **Planificacion jerarquica (HTN, Hierarchical Task Network)** (`eng::ai`). A diferencia
/// del GOAP (que busca con A* la secuencia de coste minimo), el HTN **descompone** una
/// tarea compuesta en subtareas por **metodos**: cada metodo tiene una precondicion y una
/// lista de subtareas (acciones primitivas o compuestas). El resultado es la secuencia de
/// acciones primitivas, comprobando que cada una sea aplicable en el estado que va
/// resultando. No es A*: es la descomposicion clasica con **backtracking acotado**.
///
/// Reutiliza los tipos de `goap.hpp` (`Goap<MaxFacts>::State`/`Action` y `applicable`/
/// `apply`): el HTN aporta la descomposicion, no un modelo nuevo. Determinista, sin heap y
/// sin excepciones; la recursion esta acotada por `MaxDepth` y la salida por `MaxPlan`.
///
/// Codificacion de una **subtarea** (`u16`): `t < MaxActions` = accion primitiva `t`;
/// `t >= MaxActions` = compuesta `t - MaxActions` (usar `compound(i)` en la construccion).
///
/// Verificacion: HOST-318 (incluye la equivalencia con el plan GOAP del mismo dominio).

#include <eng/ai/planning/goap.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/bitset.hpp>

namespace eng::ai {

/// Metodo HTN: precondicion (hechos) + tramo de subtareas `[first, first + count)` y
/// **prioridad** (mayor = se prueba antes; el llamador la fija, p. ej. la inversa del coste
/// estimado). A igual prioridad se respeta el orden de declaracion.
template <usize MaxFacts>
struct HtnMethod {
	eng::util::BitSet<MaxFacts> pre_true {};
	eng::util::BitSet<MaxFacts> pre_false {};
	eng::u16 first = 0u;
	eng::u16 count = 0u;
	eng::u8 priority = 0u; ///< desempate: a igual coste, mayor prioridad primero
	eng::u16 cost = 0u;    ///< coste propio del metodo (se suma al de sus acciones)
};

/// Tarea compuesta: tramo de metodos `[first, first + count)` (se prueban en orden).
struct HtnCompound {
	eng::u16 first = 0u;
	eng::u16 count = 0u;
};

/// Planificador HTN minimal: descompone una tarea compuesta en acciones primitivas.
template <usize MaxFacts, usize MaxActions, usize MaxCompounds, usize MaxMethods,
	  usize MaxSubtasks, usize MaxPlan = 24u, usize MaxDepth = 8u>
class Htn {
	static_assert(MaxMethods <= 32u, "Htn: MaxMethods <= 32 (mascara de metodos probados)");

public:
	using State = typename Goap<MaxFacts>::State;
	using Action = typename Goap<MaxFacts>::Action;
	using Facts = eng::util::BitSet<MaxFacts>;
	using Method = HtnMethod<MaxFacts>;

	/// Codifica el indice de una tarea compuesta como subtarea.
	[[nodiscard]] static constexpr eng::u16 compound(usize index) noexcept {
		return static_cast<eng::u16>(MaxActions + index);
	}

	// --- Construccion (se usa en `constexpr` desde un dominio) ---

	/// Añade un metodo (precondicion + subtareas ya volcadas con `add_subtask`). `priority`
	/// desempata a igual coste (mayor primero) y `cost` es el coste propio del metodo.
	constexpr Htn& add_method(const Facts& pre_true, const Facts& pre_false, eng::u16 first,
				  eng::u16 count, eng::u8 priority = 0u,
				  eng::u16 cost = 0u) noexcept {
		if (m_method_count < MaxMethods) {
			m_methods[m_method_count++] =
			    Method {pre_true, pre_false, first, count, priority, cost};
		}
		return *this;
	}
	/// Añade una subtarea (accion o `compound(i)`).
	constexpr Htn& add_subtask(eng::u16 subtask) noexcept {
		if (m_subtask_count < MaxSubtasks) {
			m_subtasks[m_subtask_count++] = subtask;
		}
		return *this;
	}
	/// Declara una tarea compuesta sobre los metodos `[first, first + count)`.
	constexpr Htn& add_compound(eng::u16 first, eng::u16 count) noexcept {
		if (m_compound_count < MaxCompounds) {
			m_compounds[m_compound_count++] = HtnCompound {first, count};
		}
		return *this;
	}
	[[nodiscard]] constexpr HtnCompound compound_at(eng::u16 index) const noexcept {
		return m_compounds[index];
	}

	/// Coste "sin solucion".
	static constexpr u32 no_cost = 0xffffffffu;

	/// Descompone `root` desde `start` sobre `actions` buscando la descomposicion de **coste
	/// minimo** (coste de las acciones + de los metodos). Escribe las acciones primitivas (en
	/// orden de ejecucion) en `out` y devuelve cuantas; 0 si no hay descomposicion aplicable.
	/// El coste del ultimo plan queda en `plan_cost()`.
	[[nodiscard]] constexpr usize plan(const State& start, const HtnCompound& root,
					   Span<const Action> actions, Span<u16> out) const noexcept {
		State s = start;
		usize out_n = 0u;
		m_last_cost = 0u;
		const u32 cost = solve_compound(actions, root, s, out, out_n, 0u);
		if (cost == no_cost) {
			return 0u;
		}
		m_last_cost = cost;
		return out_n;
	}

	/// Coste de la ultima descomposicion (0 si no hubo).
	[[nodiscard]] constexpr u32 plan_cost() const noexcept { return m_last_cost; }

private:
	/// ¿Se cumple la precondicion de un metodo en `s`?
	[[nodiscard]] constexpr bool pre_ok(const State& s, const Method& m) const noexcept {
		using Word = typename eng::util::BitSet<MaxFacts>::word_type;
		constexpr usize W = eng::util::BitSet<MaxFacts>::word_count;
		for (usize w = 0; w < W; ++w) {
			const Word sv = s.facts.words()[w];
			if ((sv & m.pre_true.words()[w]) != m.pre_true.words()[w]) {
				return false;
			}
			if ((sv & m.pre_false.words()[w]) != static_cast<Word>(0)) {
				return false;
			}
		}
		return true;
	}

	/// Coste minimo de descomponer las subtareas de un metodo desde `s`, escribiendo la
	/// descomposicion en `out[out_n..)`. Devuelve `no_cost` si no es aplicable.
	[[nodiscard]] constexpr u32 solve_method(Span<const Action> actions, const Method& m,
						 State& s, Span<u16> out, usize& out_n,
						 usize depth) const noexcept {
		u32 total = m.cost;
		for (eng::u16 i = 0u; i < m.count; ++i) {
			const eng::u16 t = m_subtasks[m.first + i];
			if (t < MaxActions) {
				const Action& a = actions[t];
				if (!applicable(s, a) || out_n >= out.size()) {
					return no_cost;
				}
				apply(s, a);
				out[out_n++] = t;
				total += a.cost;
			} else {
				const u32 sub = solve_compound(actions, m_compounds[t - MaxActions],
							       s, out, out_n, depth);
				if (sub == no_cost) {
					return no_cost;
				}
				total += sub;
			}
		}
		return total;
	}

	/// Evalua **todas** las descomposiciones de `c` y devuelve el coste minimo, escribiendo
	/// la mejor en `out[out_n..)`. La `priority` desempata a igual coste.
	[[nodiscard]] constexpr u32 solve_compound(Span<const Action> actions, const HtnCompound& c,
						   State& s, Span<u16> out, usize& out_n,
						   usize depth) const noexcept {
		if (depth >= MaxDepth) {
			return no_cost;
		}
		u32 best_cost = no_cost;
		eng::u8 best_prio = 0u;
		usize best_n = 0u;
		eng::u16 best_plan[MaxPlan] {};
		State best_state = s;
		for (usize mi = 0u; mi < c.count; ++mi) {
			const Method& m = m_methods[c.first + mi];
			if (!pre_ok(s, m)) {
				continue;
			}
			State s2 = s;
			eng::u16 scratch[MaxPlan] {};
			usize scratch_n = 0u;
			const u32 cost = solve_method(actions, m, s2,
						      eng::Span<eng::u16> {scratch, MaxPlan},
						      scratch_n, depth + 1u);
			if (cost == no_cost) {
				continue;
			}
			if (cost < best_cost || (cost == best_cost && m.priority > best_prio)) {
				best_cost = cost;
				best_prio = m.priority;
				best_n = scratch_n;
				best_state = s2; // efectos de la mejor descomposicion
				for (usize k = 0u; k < scratch_n; ++k) {
					best_plan[k] = scratch[k];
				}
			}
		}
		if (best_cost == no_cost || out_n + best_n > out.size()) {
			return no_cost;
		}
		s = best_state; // propaga el estado resultante al llamador
		for (usize k = 0u; k < best_n; ++k) {
			out[out_n++] = best_plan[k];
		}
		return best_cost;
	}

	Method m_methods[MaxMethods] {};
	eng::u16 m_subtasks[MaxSubtasks] {};
	HtnCompound m_compounds[MaxCompounds] {};
	usize m_method_count = 0u;
	usize m_subtask_count = 0u;
	usize m_compound_count = 0u;
	mutable u32 m_last_cost = 0u; ///< coste de la ultima descomposicion (ver `plan_cost`)
};

} // namespace eng::ai
