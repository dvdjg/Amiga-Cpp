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
/// Todo se agrupa en un **dominio** `Goap<MaxFacts>` del que cuelgan los tipos. Se
/// define una sola vez con un alias y no se repite la capacidad por el codigo:
///
///   using Ai = eng::ai::Goap<>;       // 32 hechos (clave u32), el caso normal
///   using Big = eng::ai::Goap<64>;    // 64 hechos (clave de 64 bits)
///
/// - `Ai::State`: `MaxFacts` hechos booleanos (`BitSet`). Cada hecho es un indice
///   (`Fact`); su significado (p. ej. "disk1_en_C", "horno_caliente") lo decide el juego
///   con un `enum`.
/// - `Ai::Action`: hechos requeridos a 1 (`require`), requeridos a 0 (`forbid`), hechos
///   que pone a 1 (`produce`), hechos que pone a 0 (`consume`) y `cost`.
/// - `Ai::Goal`: hechos que deben estar a 1 (`want_true`) y a 0 (`want_false`).
/// - `Ai::Planner<MaxNodes>`: A* hacia delante con heuristica de **objetivos pendientes**
///   (hechos del objetivo sin cumplir).
///
/// Consulta y aplicacion: `applicable(s, a)`, `apply(s, a)`, `satisfies(s, g)` y
/// `goal_distance(s, g)` (deducen `MaxFacts` de sus argumentos).
///
/// ## Ancho de la clave
///
/// `MaxFacts` solo admite **32** (por defecto) o **64**, que son los dos anchos utiles
/// de la clave de estado (el valor intermedio no cambiaria el codigo: `BitSet<N>` usa
/// una palabra para todo `N <= 32`). La clave de 64 bits se empaqueta en **dos palabras
/// de 32** (`StateKey64`) en vez de en un `unsigned long long`: el 68000 no tiene
/// aritmetica nativa de 64 bits y `long long` acaba en libcalls (`__ashldi3`,
/// `__lshrdi3`), mientras que dos `u32` se hashean con `hash_u32` y se comparan sin
/// libgcc.
///
/// ## Uso
///
///   enum : eng::u16 { kHarina, kHuevos, kMezcla, kHorneado };
///   using Ai = eng::ai::Goap<>;
///   constexpr eng::util::Array<Ai::Action, 3> acciones { {
///       Ai::Builder{}.named("comprar").produce(kHarina, kHuevos).build(),
///       Ai::Builder{}.named("batir").require(kHarina, kHuevos).produce(kMezcla).build(),
///       Ai::Builder{}.named("hornear").require(kMezcla).produce(kHorneado).build(),
///   } };
///   Ai::Goal meta;
///   meta.want_true.facts.set(kHorneado);
///
///   Ai::Planner<64> planner;      // unico parametro: presupuesto de nodos
///   eng::u16 plan[4];
///   const eng::usize n = planner.plan(Ai::state(), meta, acciones.span(),
///                                     eng::Span<eng::u16> {plan, 4u});
///
/// ## Coste y limites (68000)
///
/// - Pensado para **`init` o tareas de fondo**, no para el camino por frame: el
///   `Planner` reserva un `HashMap` y una `PriorityQueue` de capacidad `MaxNodes` mas
///   los nodos generados. Para un presupuesto comodo en A500, instanciar el `Planner`
///   en memoria estatica (no en la pila) y dimensionar `MaxNodes` por escenario.
///   `expansions()` dice cuanto trabajo costo el ultimo plan.
/// - La heuristica cuenta hechos del objetivo pendientes. Es admisible cuando cada
///   accion satisface como mucho un hecho del objetivo (el caso tipico de estos planes);
///   si una accion resolviera varios, la heuristica puede sobreestimar y el plan no
///   seria estrictamente optimo (seguiria siendo valido). Con coste unitario y un solo
///   hecho objetivo degenera en busqueda de anchura; el presupuesto `MaxNodes` la acota.
/// - Si el espacio de busqueda no cabe en `MaxNodes`, `plan()` devuelve `0` y
///   `found()` queda a `false` (nunca corrompe ni aborta).
///
/// Verificacion: HOST-107 (Hanoi, receta de un pastel, mision de un soldado y un
/// dominio de 64 hechos).

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/array.hpp>
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

/// Techo duro de hechos (clave de 64 bits).
inline constexpr usize goap_max_facts = 64u;

/// Indice invalido (raiz del arbol de busqueda, sin padre o sin accion).
inline constexpr u16 no_fact_link = 0xffffu;

namespace detail {

/// Clave de 64 bits en dos palabras de 32 (sin aritmetica de 64 bits en el 68000).
struct StateKey64 {
	u32 lo = 0u;
	u32 hi = 0u;
	[[nodiscard]] constexpr bool operator==(const StateKey64& other) const noexcept {
		return lo == other.lo && hi == other.hi;
	}
};

/// Ancho de la clave segun el numero de hechos: `u32` hasta 32, `StateKey64` hasta 64.
template <bool Wide>
struct KeyOf;
template <>
struct KeyOf<false> {
	using type = u32;
};
template <>
struct KeyOf<true> {
	using type = StateKey64;
};

} // namespace detail

} // namespace eng::ai

namespace eng::util {

/// `HashMap` hashea la clave de 64 bits combinando sus dos palabras (sin `mulu.l`).
template <>
struct Hash<eng::ai::detail::StateKey64> {
	[[nodiscard]] constexpr u32 operator()(const eng::ai::detail::StateKey64& k) const noexcept {
		return hash_u32(k.lo ^ rotl(k.hi, 16u));
	}
};

} // namespace eng::util

namespace eng::ai {

namespace detail {

/// Conjunto de hechos booleanos de un agente.
template <usize MaxFacts>
struct State {
	static_assert(MaxFacts == 32u || MaxFacts == 64u,
		      "State: MaxFacts debe ser 32 (clave u32) o 64 (clave de 64 bits)");

	static constexpr bool wide = (MaxFacts > 32u);
	using Key = typename KeyOf<wide>::type;
	using Word = typename eng::util::BitSet<MaxFacts>::word_type;

	eng::util::BitSet<MaxFacts> facts {};

	constexpr void set(Fact f) noexcept { facts.set(f); }
	constexpr void clear(Fact f) noexcept { facts.reset(f); }
	[[nodiscard]] constexpr bool has(Fact f) const noexcept { return facts.test(f); }
	[[nodiscard]] constexpr bool empty() const noexcept { return facts.none(); }

	/// Clave densa del estado (u32, o dos palabras si el dominio es de 64 hechos).
	[[nodiscard]] constexpr Key key() const noexcept {
		if constexpr (wide) {
			return StateKey64 {static_cast<u32>(facts.words()[0]),
					   static_cast<u32>(facts.words()[1])};
		} else {
			return static_cast<u32>(facts.words()[0]);
		}
	}

	/// Reconstruye el estado a partir de su clave (inverso de `key()`).
	[[nodiscard]] static constexpr State from_key(Key k) noexcept {
		State s {};
		if constexpr (wide) {
			s.facts.words()[0] = static_cast<Word>(k.lo);
			s.facts.words()[1] = static_cast<Word>(k.hi);
		} else {
			s.facts.words()[0] = static_cast<Word>(k);
		}
		return s;
	}
};

/// Accion GOAP: que exige y que cambia del mundo, con su coste.
template <usize MaxFacts>
struct Action {
	State<MaxFacts> pre_true {};  ///< hechos que deben estar a 1
	State<MaxFacts> pre_false {}; ///< hechos que deben estar a 0
	State<MaxFacts> eff_add {};   ///< hechos que la accion pone a 1
	State<MaxFacts> eff_del {};   ///< hechos que la accion pone a 0
	u16 cost = 1u;
	const char* name = nullptr; ///< solo diagnostico/host; puede ser `nullptr`
};

/// Objetivo: hechos que deben estar a 1 y hechos que deben estar a 0.
template <usize MaxFacts>
struct Goal {
	State<MaxFacts> want_true {};
	State<MaxFacts> want_false {};
};

/// Dominio GOAP: agrupa las acciones y el objetivo de un problema concreto. Es un
/// envoltorio sobre `Array<Action, MaxActions>` + `Goal`; el `Planner` tiene una
/// sobrecarga que lo acepta directamente (`plan(start, domain, out)`).
template <usize MaxFacts, usize MaxActions>
struct Domain {
	eng::util::Array<Action<MaxFacts>, MaxActions> actions {};
	Goal<MaxFacts> goal {};

	[[nodiscard]] constexpr Span<const Action<MaxFacts>> action_span() const noexcept {
		return actions.span();
	}
};

/// Constructor fluido de acciones (se usa en `constexpr` en los escenarios). Los hechos
/// se pasan como argumentos (`require(a, b)`, `produce(c)`), sin parametros de plantilla.
template <usize MaxFacts>
class Builder {
public:
	[[nodiscard]] constexpr Builder& named(const char* n) noexcept {
		m_action.name = n;
		return *this;
	}
	[[nodiscard]] constexpr Builder& cost(u16 c) noexcept {
		m_action.cost = c;
		return *this;
	}
	template <class... Fs>
	[[nodiscard]] constexpr Builder& require(Fs... facts) noexcept {
		((m_action.pre_true.facts.set(static_cast<Fact>(facts))), ...);
		return *this;
	}
	template <class... Fs>
	[[nodiscard]] constexpr Builder& forbid(Fs... facts) noexcept {
		((m_action.pre_false.facts.set(static_cast<Fact>(facts))), ...);
		return *this;
	}
	template <class... Fs>
	[[nodiscard]] constexpr Builder& produce(Fs... facts) noexcept {
		((m_action.eff_add.facts.set(static_cast<Fact>(facts))), ...);
		return *this;
	}
	template <class... Fs>
	[[nodiscard]] constexpr Builder& consume(Fs... facts) noexcept {
		((m_action.eff_del.facts.set(static_cast<Fact>(facts))), ...);
		return *this;
	}
	[[nodiscard]] constexpr Action<MaxFacts> build() const noexcept { return m_action; }

private:
	Action<MaxFacts> m_action {};
};

/// ¿Se cumplen las precondiciones de `a` en `s`? (requeridos a 1 presentes y
/// prohibidos ausentes). Comprobacion por palabras, sin recorrer hecho a hecho.
template <usize MaxFacts>
[[nodiscard]] constexpr bool applicable(const State<MaxFacts>& s,
					const Action<MaxFacts>& a) noexcept {
	using Word = typename eng::util::BitSet<MaxFacts>::word_type;
	constexpr usize W = eng::util::BitSet<MaxFacts>::word_count;
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
template <usize MaxFacts>
constexpr void apply(State<MaxFacts>& s, const Action<MaxFacts>& a) noexcept {
	using Word = typename eng::util::BitSet<MaxFacts>::word_type;
	constexpr usize W = eng::util::BitSet<MaxFacts>::word_count;
	for (usize w = 0; w < W; ++w) {
		s.facts.words()[w] = static_cast<Word>(
			(s.facts.words()[w] | a.eff_add.facts.words()[w]) & ~a.eff_del.facts.words()[w]);
	}
}

/// ¿Cumple `s` el objetivo `g`?
template <usize MaxFacts>
[[nodiscard]] constexpr bool satisfies(const State<MaxFacts>& s,
				       const Goal<MaxFacts>& g) noexcept {
	using Word = typename eng::util::BitSet<MaxFacts>::word_type;
	constexpr usize W = eng::util::BitSet<MaxFacts>::word_count;
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
template <usize MaxFacts>
[[nodiscard]] constexpr u16 goal_distance(const State<MaxFacts>& s,
					  const Goal<MaxFacts>& g) noexcept {
	using Word = typename eng::util::BitSet<MaxFacts>::word_type;
	constexpr usize W = eng::util::BitSet<MaxFacts>::word_count;
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
template <usize MaxFacts, usize MaxNodes>
class Planner {
	static_assert(MaxNodes > 0u, "Planner: MaxNodes debe ser mayor que 0");

public:
	using StateT = State<MaxFacts>;
	using ActionT = Action<MaxFacts>;
	using GoalT = Goal<MaxFacts>;
	using KeyT = typename StateT::Key;

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
	[[nodiscard]] constexpr usize plan(const StateT& start, const GoalT& goal,
					   Span<const ActionT> actions, Span<u16> out) noexcept {
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

			const StateT state = StateT::from_key(current.key);
			if (satisfies(state, goal)) {
				goal_node = current.node;
				m_cost = current.g;
				break;
			}
			++m_expansions;

			for (usize ai = 0; ai < actions.size(); ++ai) {
				const ActionT& a = actions[ai];
				if (!applicable(state, a)) {
					continue;
				}
				StateT next = state;
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

	/// Variante que toma un `Domain` (acciones + objetivo) en una sola pieza.
	template <usize MaxActions>
	[[nodiscard]] constexpr usize plan(const StateT& start,
					   const Domain<MaxFacts, MaxActions>& domain,
					   Span<u16> out) noexcept {
		return plan(start, domain.goal, domain.action_span(), out);
	}

private:
	struct Node {
		KeyT key;
		u16 parent;
		u16 action;
		u16 g;
	};

	struct OpenNode {
		KeyT key;
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

	[[nodiscard]] constexpr u16 add_node(KeyT key, u16 parent, u16 action, u16 g) noexcept {
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

	eng::util::HashMap<KeyT, u16, MaxNodes> m_best {};       ///< estado -> mejor coste g
	eng::util::PriorityQueue<OpenNode, MaxNodes, OpenCmp> m_open {};
	Node m_nodes[MaxNodes] {};
	usize m_node_count = 0u;
	usize m_expansions = 0u;
	u16 m_cost = 0u;
	bool m_found = false;
};

} // namespace detail

using detail::applicable;
using detail::apply;
using detail::goal_distance;
using detail::satisfies;

/// Dominio GOAP: agrupa los tipos de un mismo conjunto de hechos. Se escribe una vez
/// (`using Ai = eng::ai::Goap<>;`) y evita repetir la capacidad por el codigo.
///
/// `MaxFacts` solo admite `32` (clave `u32`, por defecto) o `64` (clave de 64 bits en
/// dos palabras); no hay valores intermedios utiles.
template <usize MaxFacts = 32u>
struct Goap {
	static_assert(MaxFacts == 32u || MaxFacts == 64u,
		      "Goap: MaxFacts debe ser 32 (clave u32) o 64 (clave de 64 bits)");

	using State = detail::State<MaxFacts>;
	using Action = detail::Action<MaxFacts>;
	using Builder = detail::Builder<MaxFacts>;
	using Goal = detail::Goal<MaxFacts>;

	template <usize MaxActions>
	using Domain = detail::Domain<MaxFacts, MaxActions>;

	template <usize MaxNodes = 128u>
	using Planner = detail::Planner<MaxFacts, MaxNodes>;

	/// Estado formado por los hechos indicados (azucar para construir escenarios; los
	/// hechos pueden ser constantes o valores de runtime).
	template <class... Fs>
	[[nodiscard]] static constexpr State state(Fs... facts) noexcept {
		State s {};
		((s.facts.set(static_cast<Fact>(facts))), ...);
		return s;
	}

	/// Objetivo con los hechos exigidos a 1 (azucar; los prohibidos se marcan aparte).
	template <class... Fs>
	[[nodiscard]] static constexpr Goal goal(Fs... want_true) noexcept {
		Goal g {};
		((g.want_true.facts.set(static_cast<Fact>(want_true))), ...);
		return g;
	}
};

} // namespace eng::ai
