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

/// Metodo HTN: precondicion (hechos) + tramo de subtareas `[first, first + count)`.
template <usize MaxFacts>
struct HtnMethod {
	eng::util::BitSet<MaxFacts> pre_true {};
	eng::util::BitSet<MaxFacts> pre_false {};
	eng::u16 first = 0u;
	eng::u16 count = 0u;
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

	/// Añade un metodo (precondicion + subtareas ya volcadas con `add_subtask`).
	constexpr Htn& add_method(const Facts& pre_true, const Facts& pre_false, eng::u16 first,
				  eng::u16 count) noexcept {
		if (m_method_count < MaxMethods) {
			m_methods[m_method_count++] = Method {pre_true, pre_false, first, count};
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

	/// Descompone `root` desde `start` sobre `actions`. Escribe las acciones primitivas (en
	/// orden de ejecucion) en `out` y devuelve cuantas; 0 si no hay descomposicion aplicable.
	[[nodiscard]] constexpr usize plan(const State& start, const HtnCompound& root,
					   Span<const Action> actions, Span<u16> out) const noexcept {
		State s = start;
		usize out_n = 0u;
		if (!decompose_compound(actions, root, s, out, out_n, 0u)) {
			return 0u;
		}
		return out_n;
	}

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

	/// Prueba los metodos de `c` en orden; el primero que descomponga gana.
	[[nodiscard]] constexpr bool decompose_compound(Span<const Action> actions,
							const HtnCompound& c, State& s, Span<u16> out,
							usize& out_n, usize depth) const noexcept {
		if (depth >= MaxDepth) {
			return false;
		}
		for (usize mi = 0; mi < c.count; ++mi) {
			const Method& m = m_methods[c.first + mi];
			if (!pre_ok(s, m)) {
				continue;
			}
			const State saved = s;
			const usize saved_out = out_n;
			if (decompose_list(actions, m, s, out, out_n, depth + 1u)) {
				return true;
			}
			s = saved; // backtracking
			out_n = saved_out;
		}
		return false;
	}

	/// Descompone las subtareas de un metodo **en orden**, aplicando cada primitiva.
	[[nodiscard]] constexpr bool decompose_list(Span<const Action> actions, const Method& m,
						    State& s, Span<u16> out, usize& out_n,
						    usize depth) const noexcept {
		for (eng::u16 i = 0u; i < m.count; ++i) {
			const eng::u16 t = m_subtasks[m.first + i];
			if (t < MaxActions) {
				const Action& a = actions[t];
				if (!applicable(s, a)) {
					return false;
				}
				if (out_n >= out.size()) {
					return false;
				}
				apply(s, a);
				out[out_n++] = t;
			} else if (!decompose_compound(
				       actions, m_compounds[t - MaxActions], s, out, out_n, depth)) {
				return false;
			}
		}
		return true;
	}

	Method m_methods[MaxMethods] {};
	eng::u16 m_subtasks[MaxSubtasks] {};
	HtnCompound m_compounds[MaxCompounds] {};
	usize m_method_count = 0u;
	usize m_subtask_count = 0u;
	usize m_compound_count = 0u;
};

} // namespace eng::ai
