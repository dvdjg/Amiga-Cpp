#pragma once

/// \file goap.hpp
/// **Planificacion GOAP** (Goal-Oriented Action Planning, `eng::ai`): el agente
/// describe el mundo con **hechos booleanos** y las acciones como **precondiciones
/// + efectos + coste**, y el planificador busca con A* la secuencia de acciones de
/// coste minimo que lleva del estado actual al objetivo.
///
/// Referencia de la tecnica: A. Alex, "Using GOAP for Advanced Gaming AI Techniques"
/// (https://arnauld-alex.com/using-goap-for-advanced-gaming-ai-techniques), basada en
/// el planificador de F.E.A.R. (Orkin). La implementacion es propia, freestanding y
/// determinista: sin `malloc`, sin excepciones y con todo el estado inline o en
/// `scratch` del llamador.
///
/// ## Modelo
///
/// - `WorldState`: hasta `max_facts` (32) hechos booleanos (`BitSet<32>`). Cada hecho
///   es un indice (`Fact`); su significado (p. ej. "disk1_en_C", "horno_caliente") lo
///   decide el juego con un `enum`.
/// - `Action`: hechos requeridos a 1 (`require`), requeridos a 0 (`forbid`), hechos que
///   pone a 1 (`produce`), hechos que pone a 0 (`consume`) y `cost`.
/// - `Goal`: hechos que deben estar a 1 (`want_true`) y a 0 (`want_false`).
/// - `Planner<MaxNodes>`: A* hacia delante con heuristica de **objetivos pendientes**
///   (hechos del objetivo sin cumplir).
///
/// El numero de hechos **no es un parametro**: el estado se empaqueta en un `u32` (una
/// palabra), asi que 32 es el limite natural y no hay que repetir el tamaño en cada
/// accion ni contenedor. Basta con crear cada `Action` y meterla en un
/// `eng::util::Array<Action, N>`; el unico parametro de la API es el presupuesto de
/// busqueda del `Planner`.
///
/// ## Uso
///
///   enum : eng::u16 { kHarina, kHuevos, kMezcla, kHorneado };
///   constexpr eng::util::Array<eng::ai::Action, 3> acciones { {
///       eng::ai::ActionBuilder{}.named("comprar").produce(kHarina, kHuevos).build(),
///       eng::ai::ActionBuilder{}.named("batir").require(kHarina, kHuevos).produce(kMezcla).build(),
///       eng::ai::ActionBuilder{}.named("hornear").require(kMezcla).produce(kHorneado).build(),
///   } };
///   eng::ai::Goal meta;
///   meta.want_true.facts.set(kHorneado);
///
///   eng::ai::Planner<64> planner;      // unica plantilla: presupuesto de nodos
///   eng::u16 plan[4];
///   const eng::usize n = planner.plan(eng::ai::make_state(), meta, acciones.span(),
///                                     eng::Span<eng::u16> {plan, 4u});
///
/// ## Coste y limites (68000)
///
/// - Pensado para **`init` o tareas de fondo**, no para el camino por frame: el
///   `Planner` reserva un `HashMap` y una `PriorityQueue` de capacidad `MaxNodes` mas
///   los nodos generados. Para un presupuesto comodo en A500, instanciar el `Planner`
///   en memoria estatica (no en la pila) y dimensionar `MaxNodes` por escenario.
///   `expansions()` dice cuanto trabajo costo el ultimo plan.
/// - `max_facts = 32`: la clave de busqueda del estado es una palabra del `BitSet`, de
///   ahi la deduplicacion `O(1)` sin hashes largos. Un indice fuera de `[0, 32)` dispara
///   `illegal` (contrato de `BitSet`).
/// - La heuristica cuenta hechos del objetivo pendientes. Es admisible cuando cada
///   accion satisface como mucho un hecho del objetivo (el caso tipico de estos planes);
///   si una accion resolviera varios, la heuristica puede sobreestimar y el plan no
///   seria estrictamente optimo (seguiria siendo valido). Con coste unitario y un solo
///   hecho objetivo degenera en busqueda de anchura; el presupuesto `MaxNodes` la acota.
/// - Si el espacio de busqueda no cabe en `MaxNodes`, `plan()` devuelve `0` y
///   `found()` queda a `false` (nunca corrompe ni aborta).
///
/// Verificacion: HOST-107 (Hanoi, receta de un pastel y mision de un soldado).

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/bit.hpp>
#include <eng/core/util/bitset.hpp>
#include <eng/core/util/hash_map.hpp>
#include <eng/core/util/priority_queue.hpp>

namespace eng::ai {

using eng::u16;
using eng::u32;
using eng::usize;

/// Indice de un hecho booleano del mundo.
using Fact = eng::u16;

/// Numero maximo de hechos: el estado se empaqueta en un `u32`, de ahi el tope.
inline constexpr usize max_facts = 32u;

/// Indice invalido (raiz del arbol de busqueda, sin padre o sin accion).
inline constexpr u16 no_fact_link = 0xffffu;

/// Conjunto de hechos booleanos de un agente.
struct WorldState {
	eng::util::BitSet<max_facts> facts {};

	constexpr void set(Fact f) noexcept { facts.set(f); }
	constexpr void clear(Fact f) noexcept { facts.reset(f); }
	[[nodiscard]] constexpr bool has(Fact f) const noexcept { return facts.test(f); }
	[[nodiscard]] constexpr bool empty() const noexcept { return facts.none(); }

	/// Clave densa del estado (unica para 32 hechos) para deduplicar en la busqueda.
	[[nodiscard]] constexpr u32 key() const noexcept { return facts.words()[0]; }
};

/// Estado formado por los hechos indicados (azucar para construir escenarios; los
/// hechos pueden ser constantes o valores de runtime).
template <class... Fs>
[[nodiscard]] constexpr WorldState make_state(Fs... facts) noexcept {
	WorldState s {};
	((s.facts.set(static_cast<Fact>(facts))), ...);
	return s;
}

/// Accion GOAP: que exige y que cambia del mundo, con su coste.
struct Action {
	WorldState pre_true {};  ///< hechos que deben estar a 1
	WorldState pre_false {}; ///< hechos que deben estar a 0
	WorldState eff_add {};   ///< hechos que la accion pone a 1
	WorldState eff_del {};   ///< hechos que la accion pone a 0
	u16 cost = 1u;
	const char* name = nullptr; ///< solo diagnostico/host; puede ser `nullptr`
};

/// Constructor fluido de acciones (se usa en `constexpr` en los escenarios). Los hechos
/// se pasan como argumentos (`require(a, b)`, `produce(c)`), sin parametros de plantilla.
class ActionBuilder {
public:
	[[nodiscard]] constexpr ActionBuilder& named(const char* n) noexcept {
		m_action.name = n;
		return *this;
	}
	[[nodiscard]] constexpr ActionBuilder& cost(u16 c) noexcept {
		m_action.cost = c;
		return *this;
	}
	template <class... Fs>
	[[nodiscard]] constexpr ActionBuilder& require(Fs... facts) noexcept {
		((m_action.pre_true.facts.set(static_cast<Fact>(facts))), ...);
		return *this;
	}
	template <class... Fs>
	[[nodiscard]] constexpr ActionBuilder& forbid(Fs... facts) noexcept {
		((m_action.pre_false.facts.set(static_cast<Fact>(facts))), ...);
		return *this;
	}
	template <class... Fs>
	[[nodiscard]] constexpr ActionBuilder& produce(Fs... facts) noexcept {
		((m_action.eff_add.facts.set(static_cast<Fact>(facts))), ...);
		return *this;
	}
	template <class... Fs>
	[[nodiscard]] constexpr ActionBuilder& consume(Fs... facts) noexcept {
		((m_action.eff_del.facts.set(static_cast<Fact>(facts))), ...);
		return *this;
	}
	[[nodiscard]] constexpr Action build() const noexcept { return m_action; }

private:
	Action m_action {};
};

/// Objetivo: hechos que deben estar a 1 y hechos que deben estar a 0.
struct Goal {
	WorldState want_true {};
	WorldState want_false {};
};

/// ¿Se cumplen las precondiciones de `a` en `s`? (requeridos a 1 presentes y
/// prohibidos ausentes). Comprobacion por palabras, sin recorrer hecho a hecho.
[[nodiscard]] constexpr bool applicable(const WorldState& s, const Action& a) noexcept {
	using Word = eng::util::BitSet<max_facts>::word_type;
	constexpr usize W = eng::util::BitSet<max_facts>::word_count;
	for (usize w = 0; w < W; ++w) {
		const Word sv = s.facts.words()[w];
		if ((sv & a.pre_true.facts.words()[w]) != a.pre_true.facts.words()[w]) {
			return false;
		}
		if ((sv & a.pre_false.facts.words()[w]) != static_cast<Word>(0)) {
			return false;
		}
	}
	return true;
}

/// Aplica los efectos de `a` sobre `s` (primero añade, luego elimina). La accion debe
/// ser `applicable`; aplicar una no aplicable da un estado incoherente sin avisar.
constexpr void apply(WorldState& s, const Action& a) noexcept {
	using Word = eng::util::BitSet<max_facts>::word_type;
	constexpr usize W = eng::util::BitSet<max_facts>::word_count;
	for (usize w = 0; w < W; ++w) {
		s.facts.words()[w] = static_cast<Word>(
			(s.facts.words()[w] | a.eff_add.facts.words()[w]) & ~a.eff_del.facts.words()[w]);
	}
}

/// ¿Cumple `s` el objetivo `g`?
[[nodiscard]] constexpr bool satisfies(const WorldState& s, const Goal& g) noexcept {
	using Word = eng::util::BitSet<max_facts>::word_type;
	constexpr usize W = eng::util::BitSet<max_facts>::word_count;
	for (usize w = 0; w < W; ++w) {
		const Word sv = s.facts.words()[w];
		if ((sv & g.want_true.facts.words()[w]) != g.want_true.facts.words()[w]) {
			return false;
		}
		if ((sv & g.want_false.facts.words()[w]) != static_cast<Word>(0)) {
			return false;
		}
	}
	return true;
}

/// Heuristica: cuantos hechos del objetivo quedan por cumplir.
[[nodiscard]] constexpr u16 goal_distance(const WorldState& s, const Goal& g) noexcept {
	using Word = eng::util::BitSet<max_facts>::word_type;
	constexpr usize W = eng::util::BitSet<max_facts>::word_count;
	u16 d = 0u;
	for (usize w = 0; w < W; ++w) {
		const Word sv = s.facts.words()[w];
		const Word want = g.want_true.facts.words()[w];
		const Word avoid = g.want_false.facts.words()[w];
		d = static_cast<u16>(d + eng::util::popcount(static_cast<Word>(want & ~sv)));
		d = static_cast<u16>(d + eng::util::popcount(static_cast<Word>(avoid & sv)));
	}
	return d;
}

/// Planificador GOAP: A* hacia delante sobre el espacio de hechos.
///
/// `MaxNodes` es el presupuesto comun de nodos generados, entradas del mapa de mejor
/// coste y elementos de la cola. El estado de trabajo va inline en el objeto, asi que
/// conviene instanciarlo en estatica para no consumir pila en el 68000.
template <usize MaxNodes = 128u>
class Planner {
	static_assert(MaxNodes > 0u, "Planner: MaxNodes debe ser mayor que 0");

public:
	/// ¿La ultima llamada a `plan()` encontro una solucion? (un plan vacio es valido:
	/// el estado inicial ya cumplia el objetivo).
	[[nodiscard]] constexpr bool found() const noexcept { return m_found; }

	/// Coste total del ultimo plan (0 si vacio o no encontrado).
	[[nodiscard]] constexpr u16 plan_cost() const noexcept { return m_cost; }

	/// Nodos expandidos en la ultima busqueda (diagnostico de presupuesto).
	[[nodiscard]] constexpr usize expansions() const noexcept { return m_expansions; }

	/// Busca el plan de coste minimo de `start` a `goal` con `actions` y escribe los
	/// indices de accion (de primero a ultimo) en `out`. Devuelve el numero de acciones
	/// (0 si el plan es vacio o no se encontro; ver `found()`), o 0 si no cabe en `out`.
	[[nodiscard]] constexpr usize plan(const WorldState& start, const Goal& goal,
					   Span<const Action> actions, Span<u16> out) noexcept {
		m_found = false;
		m_cost = 0u;
		m_expansions = 0u;
		m_node_count = 0u;
		m_best.clear();
		m_open.clear();

		if (satisfies(start, goal)) {
			m_found = true;
			return 0u;
		}
		if (actions.empty() || out.empty()) {
			return 0u;
		}

		const u16 root = add_node(start.key(), no_fact_link, no_fact_link, 0u);
		m_best.insert(start.key(), 0u);
		m_open.push(OpenNode {start.key(), 0u, goal_distance(start, goal), root});

		u16 goal_node = no_fact_link;
		while (!m_open.empty()) {
			const OpenNode current = m_open.top();
			m_open.pop();
			const u16* best = m_best.find(current.key);
			if (best == nullptr || *best != current.g) {
				continue; // entrada obsoleta: se relajo a un coste menor despues
			}

			WorldState state {};
			state.facts.words()[0] = static_cast<Word>(current.key);
			if (satisfies(state, goal)) {
				goal_node = current.node;
				m_cost = current.g;
				break;
			}
			++m_expansions;

			for (usize ai = 0; ai < actions.size(); ++ai) {
				const Action& a = actions[ai];
				if (!applicable(state, a)) {
					continue;
				}
				WorldState next = state;
				apply(next, a);
				const u16 ng = static_cast<u16>(current.g + a.cost);
				const u16* seen = m_best.find(next.key());
				if (seen != nullptr && *seen <= ng) {
					continue;
				}
				if (m_node_count >= MaxNodes) {
					break; // presupuesto de nodos agotado
				}
				const u16 child = add_node(next.key(), current.node,
							   static_cast<u16>(ai), ng);
				m_best.insert_or_assign(next.key(), ng);
				const u32 f = static_cast<u32>(ng) + goal_distance(next, goal);
				const u16 fc = f > 0xffffu ? static_cast<u16>(0xffffu)
							   : static_cast<u16>(f);
				if (!m_open.push(OpenNode {next.key(), ng, fc, child})) {
					return 0u; // cola llena: no hay hueco para mas candidatos
				}
			}
		}

		if (goal_node == no_fact_link) {
			return 0u;
		}
		m_found = true;
		return reconstruct(goal_node, out);
	}

private:
	using Word = eng::util::BitSet<max_facts>::word_type;

	struct Node {
		u32 key;
		u16 parent;
		u16 action;
		u16 g;
	};

	struct OpenNode {
		u32 key;
		u16 g;
		u16 f;
		u16 node;
	};

	struct OpenCmp {
		[[nodiscard]] constexpr bool operator()(const OpenNode& a,
							const OpenNode& b) const noexcept {
			if (a.f != b.f) {
				return a.f > b.f; // min-heap por f
			}
			return a.node > b.node; // desempate determinista (orden de creacion)
		}
	};

	[[nodiscard]] constexpr u16 add_node(u32 key, u16 parent, u16 action, u16 g) noexcept {
		const u16 index = static_cast<u16>(m_node_count);
		m_nodes[m_node_count] = Node {key, parent, action, g};
		++m_node_count;
		return index;
	}

	[[nodiscard]] constexpr usize reconstruct(u16 goal_node, Span<u16> out) const noexcept {
		usize count = 0u;
		for (u16 n = goal_node; m_nodes[n].parent != no_fact_link;
		     n = m_nodes[n].parent) {
			++count;
		}
		if (count > out.size()) {
			return 0u;
		}
		u16 n = goal_node;
		for (usize i = count; i > 0u; --i) {
			out[i - 1u] = m_nodes[n].action;
			n = m_nodes[n].parent;
		}
		return count;
	}

	eng::util::HashMap<u32, u16, MaxNodes> m_best {};        ///< estado -> mejor coste g
	eng::util::PriorityQueue<OpenNode, MaxNodes, OpenCmp> m_open {};
	Node m_nodes[MaxNodes] {};
	usize m_node_count = 0u;
	usize m_expansions = 0u;
	u16 m_cost = 0u;
	bool m_found = false;
};

} // namespace eng::ai
