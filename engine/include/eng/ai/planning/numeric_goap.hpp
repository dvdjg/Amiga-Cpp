#pragma once

/// \file numeric_goap.hpp
/// **Planificación GOAP con variables numéricas cuantizadas** (`eng::ai`): además de
/// los hechos booleanos de `goap.hpp`, el estado lleva hasta 4 variables de **nivel**
/// (`u8`, 0..255). Los enteros son niveles directos; los decimales, niveles escalados
/// (p. ej. `Fixed` q4.4 = nivel/16). Al discretizar, la clave sigue cabiendo en 64
/// bits (`u32` hechos + `u32` variables) y el planner mantiene el coste y el
/// determinismo del original, sin heap.
///
/// Modelo de una acción:
/// - booleanos: `require`/`forbid`/`produce`/`consume`;
/// - numéricos: `var_ge(v, min)` (precondición `>=`), `var_le(v, max)` (`<=`),
///   `add(v, delta)`, `sub(v, delta)` y `set_var(v, nivel)` (efectos, saturados a
///   0..255).
///
/// Caché (tácticas de aceleración):
/// 1. **Memo de planes** por `(start, goal)`: `plan_cached` devuelve el plan guardado
///    sin buscar (pool de acciones, sin heap).
/// 2. **Reutilización de sufijo**: `plan_reusing` valida el sufijo del plan anterior
///    tras ejecutar un paso y, si sigue siendo aplicable y cumple el objetivo, lo
///    devuelve sin abrir búsqueda.
/// 3. **Closed set** por búsqueda (`m_best`), como en el GOAP booleano.
///
/// Límites: `MaxFacts <= 32` y `MaxVars <= 4` (clave de 64 bits exacta). Para más
/// variables, componer dominios o usar el planner numérico general (fase B).
///
/// Verificación: HOST-158.

#include <eng/ai/planning/goap.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/bit.hpp>
#include <eng/core/util/bitset.hpp>
#include <eng/core/util/hash.hpp>
#include <eng/core/util/hash_map.hpp>
#include <eng/core/util/priority_queue.hpp>

namespace eng::ai {

using eng::u16;
using eng::u32;
using eng::usize;

inline constexpr usize numeric_goap_max_vars = 4u;
inline constexpr usize numeric_goap_max_facts = 32u;
inline constexpr u8 numeric_goap_no_level = 0xffu;

namespace numeric_detail {

using Facts = eng::util::BitSet<numeric_goap_max_facts>;

/// Variable numérica "sin valor" para `var_le` (sin cota superior).
inline constexpr u8 var_no_max = 0xffu;
/// Efecto `add`/`sub` no fijado.
inline constexpr eng::s8 var_no_delta = 0;

/// Clave exacta del estado: palabra de hechos + 4 niveles (64 bits).
struct NumericKey {
	u32 facts = 0u;
	u32 vars = 0u; ///< 4 niveles empaquetados (little-endian de bytes)
	[[nodiscard]] constexpr bool operator==(const NumericKey& other) const noexcept {
		return facts == other.facts && vars == other.vars;
	}
};

/// Estado: hechos booleanos + `MaxVars` niveles (0..255).
template <usize MaxVars>
struct NumericState {
	static_assert(MaxVars >= 1u && MaxVars <= numeric_goap_max_vars,
		      "NumericState: MaxVars en 1..4 (clave de 64 bits)");

	Facts facts {};
	u8 vars[MaxVars] {};

	[[nodiscard]] constexpr u8 var(usize i) const noexcept { return vars[i]; }
	constexpr void set_var(usize i, u8 level) noexcept { vars[i] = level; }

	[[nodiscard]] constexpr NumericKey key() const noexcept {
		NumericKey k {};
		k.facts = static_cast<u32>(facts.words()[0]);
		u32 packed = 0u;
		for (usize i = 0; i < MaxVars; ++i) {
			packed |= static_cast<u32>(vars[i]) << (8u * static_cast<u32>(i));
		}
		k.vars = packed;
		return k;
	}

	[[nodiscard]] static constexpr NumericState from_key(NumericKey k) noexcept {
		NumericState s {};
		s.facts.words()[0] = static_cast<typename Facts::word_type>(k.facts);
		for (usize i = 0; i < MaxVars; ++i) {
			s.vars[i] = static_cast<u8>((k.vars >> (8u * static_cast<u32>(i))) & 0xffu);
		}
		return s;
	}
};

/// Acción numérica.
template <usize MaxVars>
struct NumericAction {
	Facts pre_true {};
	Facts pre_false {};
	Facts eff_add {};
	Facts eff_del {};

	u8 var_min[MaxVars] {};                             ///< pre: var >= var_min
	u8 var_max[MaxVars] {};                             ///< pre: var <= var_max (var_no_max = sin cota)
	eng::s8 var_add[MaxVars] {};                        ///< efecto: var += add (puede ser negativo)
	u8 var_set[MaxVars] {};                             ///< efecto: var = set (no_level = ninguno)

	u16 cost = 1u;
	const char* name = nullptr;

	constexpr NumericAction() noexcept {
		for (usize i = 0; i < MaxVars; ++i) {
			var_max[i] = var_no_max;              // sin cota superior por defecto
			var_set[i] = numeric_goap_no_level;    // sin asignación por defecto
		}
	}
};

/// Objetivo: hechos + umbrales numéricos (`var >= var_ge`, `var <= var_le`).
template <usize MaxVars>
struct NumericGoal {
	Facts want_true {};
	Facts want_false {};
	u8 var_ge[MaxVars] {};    ///< exigido var >= var_ge (0 = ninguno)
	u8 var_le[MaxVars] {};    ///< exigido var <= var_le (var_no_max = ninguno)

	constexpr NumericGoal() noexcept {
		for (usize i = 0; i < MaxVars; ++i) {
			var_le[i] = var_no_max; // sin cota superior por defecto
		}
	}
};

/// Constructor fluido.
template <usize MaxVars>
class NumericBuilder {
public:
	[[nodiscard]] constexpr NumericBuilder& named(const char* n) noexcept {
		m_action.name = n;
		return *this;
	}
	[[nodiscard]] constexpr NumericBuilder& cost(u16 c) noexcept {
		m_action.cost = c;
		return *this;
	}
	template <class... Fs>
	[[nodiscard]] constexpr NumericBuilder& require(Fs... f) noexcept {
		((m_action.pre_true.set(static_cast<Fact>(f))), ...);
		return *this;
	}
	template <class... Fs>
	[[nodiscard]] constexpr NumericBuilder& forbid(Fs... f) noexcept {
		((m_action.pre_false.set(static_cast<Fact>(f))), ...);
		return *this;
	}
	template <class... Fs>
	[[nodiscard]] constexpr NumericBuilder& produce(Fs... f) noexcept {
		((m_action.eff_add.set(static_cast<Fact>(f))), ...);
		return *this;
	}
	template <class... Fs>
	[[nodiscard]] constexpr NumericBuilder& consume(Fs... f) noexcept {
		((m_action.eff_del.set(static_cast<Fact>(f))), ...);
		return *this;
	}
	[[nodiscard]] constexpr NumericBuilder& var_ge(usize v, u8 min) noexcept {
		m_action.var_min[v] = min;
		return *this;
	}
	[[nodiscard]] constexpr NumericBuilder& var_le(usize v, u8 max) noexcept {
		m_action.var_max[v] = max;
		return *this;
	}
	[[nodiscard]] constexpr NumericBuilder& add(usize v, int delta) noexcept {
		m_action.var_add[v] = static_cast<eng::s8>(delta);
		return *this;
	}
	[[nodiscard]] constexpr NumericBuilder& set_var(usize v, u8 level) noexcept {
		m_action.var_set[v] = level;
		return *this;
	}
	[[nodiscard]] constexpr NumericAction<MaxVars> build() const noexcept { return m_action; }

private:
	NumericAction<MaxVars> m_action {};
};

[[nodiscard]] constexpr u8 sat_level(int value) noexcept {
	if (value < 0) {
		return 0u;
	}
	if (value > 255) {
		return 255u;
	}
	return static_cast<u8>(value);
}

template <usize MaxVars>
[[nodiscard]] constexpr bool applicable(const NumericState<MaxVars>& s,
					const NumericAction<MaxVars>& a) noexcept {
	const u32 facts = static_cast<u32>(s.facts.words()[0]);
	if ((facts & static_cast<u32>(a.pre_true.words()[0])) !=
	    static_cast<u32>(a.pre_true.words()[0])) {
		return false;
	}
	if ((facts & static_cast<u32>(a.pre_false.words()[0])) != 0u) {
		return false;
	}
	for (usize i = 0; i < MaxVars; ++i) {
		if (a.var_min[i] != 0u && s.vars[i] < a.var_min[i]) {
			return false;
		}
		if (a.var_max[i] != var_no_max && s.vars[i] > a.var_max[i]) {
			return false;
		}
	}
	return true;
}

template <usize MaxVars>
constexpr void apply(NumericState<MaxVars>& s, const NumericAction<MaxVars>& a) noexcept {
	s.facts.words()[0] = static_cast<typename Facts::word_type>(
	    (static_cast<u32>(s.facts.words()[0]) | static_cast<u32>(a.eff_add.words()[0])) &
	    ~static_cast<u32>(a.eff_del.words()[0]));
	for (usize i = 0; i < MaxVars; ++i) {
		if (a.var_set[i] != numeric_goap_no_level) {
			s.vars[i] = a.var_set[i];
		} else if (a.var_add[i] != var_no_delta) {
			s.vars[i] = sat_level(static_cast<int>(s.vars[i]) + a.var_add[i]);
		}
	}
}

template <usize MaxVars>
[[nodiscard]] constexpr bool satisfies(const NumericState<MaxVars>& s,
				       const NumericGoal<MaxVars>& g) noexcept {
	const u32 facts = static_cast<u32>(s.facts.words()[0]);
	if ((facts & static_cast<u32>(g.want_true.words()[0])) !=
	    static_cast<u32>(g.want_true.words()[0])) {
		return false;
	}
	if ((facts & static_cast<u32>(g.want_false.words()[0])) != 0u) {
		return false;
	}
	for (usize i = 0; i < MaxVars; ++i) {
		if (g.var_ge[i] != 0u && s.vars[i] < g.var_ge[i]) {
			return false;
		}
		if (g.var_le[i] != var_no_max && s.vars[i] > g.var_le[i]) {
			return false;
		}
	}
	return true;
}

/// Heurística: hechos del objetivo pendientes + variables por debajo del umbral.
template <usize MaxVars>
[[nodiscard]] constexpr u16 goal_distance(const NumericState<MaxVars>& s,
					  const NumericGoal<MaxVars>& g) noexcept {
	const u32 facts = static_cast<u32>(s.facts.words()[0]);
	u16 d = eng::util::popcount(static_cast<u32>(static_cast<u32>(g.want_true.words()[0]) & ~facts));
	d = static_cast<u16>(d + eng::util::popcount(static_cast<u32>(
				 static_cast<u32>(g.want_false.words()[0]) & facts)));
	for (usize i = 0; i < MaxVars; ++i) {
		if (g.var_ge[i] != 0u && s.vars[i] < g.var_ge[i]) {
			++d;
		}
		if (g.var_le[i] != var_no_max && s.vars[i] > g.var_le[i]) {
			++d;
		}
	}
	return d;
}

/// Planificador numérico A* (misma estructura y cachés que el GOAP booleano).
template <usize MaxVars, usize MaxNodes, usize MaxCachedPlans = 4u,
	  usize MaxCachedActions = 64u>
class NumericPlanner {
	static_assert(MaxNodes > 0u, "NumericPlanner: MaxNodes > 0");

public:
	using StateT = NumericState<MaxVars>;
	using ActionT = NumericAction<MaxVars>;
	using GoalT = NumericGoal<MaxVars>;
	using KeyT = NumericKey;

	[[nodiscard]] constexpr bool found() const noexcept { return m_found; }
	[[nodiscard]] constexpr u16 plan_cost() const noexcept { return m_cost; }
	[[nodiscard]] constexpr usize expansions() const noexcept { return m_expansions; }

	[[nodiscard]] constexpr usize plan(const StateT& start, const GoalT& goal,
					   Span<const ActionT> actions, Span<u16> out) noexcept {
		return plan_impl<false>(start, goal, actions, out);
	}

	/// Variante con **heurística de grafo relajado** (h_max sobre hechos + cota
	/// numérica) y **memo de heurística** entre llamadas (reutiliza valores h de
	/// estados ya vistos con el mismo dominio). Guía mejor la búsqueda; no garantiza
	/// optimalidad estricta (h_max no es admisible en general), pero devuelve un plan
	/// válido.
	[[nodiscard]] constexpr usize plan_relaxed(const StateT& start, const GoalT& goal,
						   Span<const ActionT> actions, Span<u16> out) noexcept {
		return plan_impl<true>(start, goal, actions, out);
	}

	[[nodiscard]] constexpr usize heuristic_hits() const noexcept { return m_h_hits; }
	constexpr void clear_heuristic_cache() noexcept {
		m_h_cache.clear();
		m_h_hits = 0u;
	}

	template <bool Relaxed>
	[[nodiscard]] constexpr usize plan_impl(const StateT& start, const GoalT& goal,
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
		m_open.push(OpenNode {start.key(), 0u, heuristic<Relaxed>(start, goal, actions), root});

		u16 goal_node = no_fact_link;
		while (!m_open.empty()) {
			const OpenNode current = m_open.top();
			m_open.pop();
			const u16* best = m_best.find(current.key);
			if (best == nullptr || *best != current.g) {
				continue;
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
					break;
				}
				const u16 child = add_node(next.key(), current.node, static_cast<u16>(ai), ng);
				m_best.insert_or_assign(next.key(), ng);
				const u32 f = static_cast<u32>(ng) + heuristic<Relaxed>(next, goal, actions);
				const u16 fc = f > 0xffffu ? static_cast<u16>(0xffffu) : static_cast<u16>(f);
				if (!m_open.push(OpenNode {next.key(), ng, fc, child})) {
					return 0u;
				}
			}
		}
		if (goal_node == no_fact_link) {
			return 0u;
		}
		m_found = true;
		return reconstruct(goal_node, out);
	}

	/// Memo de planes por `(start, goal)`: repite la consulta sin buscar.
	[[nodiscard]] constexpr usize plan_cached(const StateT& start, const GoalT& goal,
						  Span<const ActionT> actions,
						  Span<u16> out) noexcept {
		const KeyT k_start = start.key();
		const u32 k_goal = goal_signature(goal);
		for (usize i = 0; i < m_cache_count; ++i) {
			const CacheEntry& e = m_cache[i];
			if (e.start == k_start && e.goal == k_goal) {
				if (e.length > out.size()) {
					break;
				}
				for (usize j = 0; j < e.length; ++j) {
					out[j] = m_plan_pool[e.offset + j];
				}
				m_found = true;
				m_cost = e.cost;
				m_expansions = 0u;
				return e.length;
			}
		}
		const usize n = plan(start, goal, actions, out);
		if (m_found && m_cache_count < MaxCachedPlans && m_plan_used + n <= MaxCachedActions) {
			CacheEntry& e = m_cache[m_cache_count++];
			e.start = k_start;
			e.goal = k_goal;
			e.offset = m_plan_used;
			e.length = static_cast<u16>(n);
			e.cost = m_cost;
			for (usize j = 0; j < n; ++j) {
				m_plan_pool[m_plan_used + j] = out[j];
			}
			m_plan_used = static_cast<u16>(m_plan_used + n);
		}
		return n;
	}

	/// Reutiliza el **sufijo** del plan anterior tras ejecutar el paso `taken`: si el
	/// resto sigue siendo aplicable desde `start` y cumple el objetivo, lo devuelve sin
	/// buscar; si no, replantea.
	[[nodiscard]] constexpr usize plan_reusing(const StateT& start, const GoalT& goal,
						   Span<const ActionT> actions, Span<u16> out,
						   Span<const u16> previous, usize taken) noexcept {
		StateT s = start;
		u16 cost = 0u;
		usize n = 0u;
		bool ok = true;
		for (usize i = taken + 1u; i < previous.size(); ++i) {
			const u16 ai = previous[i];
			if (ai >= actions.size() || !applicable(s, actions[ai])) {
				ok = false;
				break;
			}
			apply(s, actions[ai]);
			cost = static_cast<u16>(cost + actions[ai].cost);
			++n;
		}
		if (ok && n <= out.size() && satisfies(s, goal)) {
			for (usize j = 0; j < n; ++j) {
				out[j] = previous[taken + 1u + j];
			}
			m_found = true;
			m_cost = cost;
			m_expansions = 0u;
			return n;
		}
		return plan_cached(start, goal, actions, out);
	}

	constexpr void clear_plan_cache() noexcept {
		m_cache_count = 0u;
		m_plan_used = 0u;
	}

private:
	/// Heurística de grafo relajado: h_max sobre hechos (ignorando efectos de borrado)
	/// + cota numérica por el mayor delta por acción. Con memo por clave de estado.
	[[nodiscard]] constexpr u16 relaxed_distance(const StateT& s, const GoalT& g,
						     Span<const ActionT> actions) noexcept {
		const KeyT k = s.key();
		if (const u16* cached = m_h_cache.find(k)) {
			++m_h_hits;
			return *cached;
		}
		constexpr u16 inf = 0xffffu;
		u16 fact_cost[numeric_goap_max_facts];
		for (usize f = 0; f < numeric_goap_max_facts; ++f) {
			fact_cost[f] = s.facts.test(f) ? 0u : inf;
		}
		bool changed = true;
		usize guard = 0u;
		while (changed && guard < 64u) {
			changed = false;
			++guard;
			for (usize ai = 0; ai < actions.size(); ++ai) {
				const ActionT& a = actions[ai];
				u16 pre = 0u;
				bool ok = true;
				for (usize f = 0; f < numeric_goap_max_facts; ++f) {
					if (a.pre_true.test(f)) {
						if (fact_cost[f] == inf) {
							ok = false;
							break;
						}
						if (fact_cost[f] > pre) {
							pre = fact_cost[f];
						}
					}
				}
				if (!ok) {
					continue;
				}
				for (usize f = 0; f < numeric_goap_max_facts; ++f) {
					if (a.eff_add.test(f)) {
						const u16 cand = static_cast<u16>(a.cost + pre);
						if (cand < fact_cost[f]) {
							fact_cost[f] = cand;
							changed = true;
						}
					}
				}
			}
		}
		u16 h = 0u;
		for (usize f = 0; f < numeric_goap_max_facts; ++f) {
			if (g.want_true.test(f)) {
				h = static_cast<u16>(h + (fact_cost[f] == inf ? 255u : fact_cost[f]));
			}
			if (g.want_false.test(f) && s.facts.test(f)) {
				h = static_cast<u16>(h + 1u);
			}
		}
		for (usize i = 0; i < MaxVars; ++i) {
			if (g.var_ge[i] != 0u && s.vars[i] < g.var_ge[i]) {
				int best_delta = 0;
				u16 min_cost = inf;
				for (usize ai = 0; ai < actions.size(); ++ai) {
					const int d = actions[ai].var_add[i];
					if (d > best_delta) {
						best_delta = d;
						min_cost = actions[ai].cost;
					} else if (d == best_delta && d > 0 && actions[ai].cost < min_cost) {
						min_cost = actions[ai].cost;
					}
				}
				const int needed = static_cast<int>(g.var_ge[i]) - static_cast<int>(s.vars[i]);
				if (best_delta <= 0) {
					h = static_cast<u16>(h + 255u);
				} else {
					const u16 steps = static_cast<u16>((needed + best_delta - 1) / best_delta);
					h = static_cast<u16>(h + steps * (min_cost == inf ? 1u : min_cost));
				}
			}
		}
		m_h_cache.insert_or_assign(k, h);
		return h;
	}

	template <bool Relaxed>
	[[nodiscard]] constexpr u16 heuristic(const StateT& s, const GoalT& g,
					      Span<const ActionT> actions) noexcept {
		(void)actions;
		if constexpr (Relaxed) {
			return relaxed_distance(s, g, actions);
		} else {
			return goal_distance(s, g);
		}
	}

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
				return a.f > b.f;
			}
			return a.node > b.node;
		}
	};
	struct CacheEntry {
		KeyT start {};
		u32 goal = 0u;
		u16 offset = 0u;
		u16 length = 0u;
		u16 cost = 0u;
	};

	[[nodiscard]] static constexpr u32 goal_signature(const GoalT& g) noexcept {
		u32 sig = static_cast<u32>(g.want_true.words()[0]) ^
			  eng::util::rotl(static_cast<u32>(g.want_false.words()[0]), 16u);
		for (usize i = 0; i < MaxVars; ++i) {
			sig = eng::util::hash_u32(sig ^
						  (static_cast<u32>(g.var_ge[i]) << 8u) ^
						  static_cast<u32>(g.var_le[i]));
		}
		return sig;
	}

	[[nodiscard]] constexpr u16 add_node(KeyT key, u16 parent, u16 action, u16 g) noexcept {
		const u16 index = static_cast<u16>(m_node_count);
		m_nodes[m_node_count] = Node {key, parent, action, g};
		++m_node_count;
		return index;
	}

	[[nodiscard]] constexpr usize reconstruct(u16 goal_node, Span<u16> out) const noexcept {
		usize count = 0u;
		for (u16 node = goal_node; m_nodes[node].parent != no_fact_link;
		     node = m_nodes[node].parent) {
			++count;
		}
		if (count > out.size()) {
			return 0u;
		}
		u16 node = goal_node;
		for (usize i = count; i > 0u; --i) {
			out[i - 1u] = m_nodes[node].action;
			node = m_nodes[node].parent;
		}
		return count;
	}

	eng::util::HashMap<KeyT, u16, MaxNodes> m_best {};
	eng::util::HashMap<KeyT, u16, MaxNodes> m_h_cache {};
	usize m_h_hits = 0u;
	eng::util::PriorityQueue<OpenNode, MaxNodes, OpenCmp> m_open {};
	Node m_nodes[MaxNodes] {};
	usize m_node_count = 0u;
	usize m_expansions = 0u;
	u16 m_cost = 0u;
	bool m_found = false;

	CacheEntry m_cache[MaxCachedPlans] {};
	usize m_cache_count = 0u;
	u16 m_plan_pool[MaxCachedActions] {};
	u16 m_plan_used = 0u;
};

} // namespace numeric_detail

using numeric_detail::applicable;
using numeric_detail::apply;
using numeric_detail::goal_distance;
using numeric_detail::satisfies;

/// Dominio numérico: agrupa los tipos con `MaxVars` variables de nivel.
template <usize MaxVars = numeric_goap_max_vars>
struct NumericGoap {
	static_assert(MaxVars >= 1u && MaxVars <= numeric_goap_max_vars,
		      "NumericGoap: MaxVars en 1..4");

	using State = numeric_detail::NumericState<MaxVars>;
	using Action = numeric_detail::NumericAction<MaxVars>;
	using Builder = numeric_detail::NumericBuilder<MaxVars>;
	using Goal = numeric_detail::NumericGoal<MaxVars>;

	template <usize MaxNodes = 128u, usize MaxCachedPlans = 4u, usize MaxCachedActions = 64u>
	using Planner = numeric_detail::NumericPlanner<MaxVars, MaxNodes, MaxCachedPlans,
						      MaxCachedActions>;
};

} // namespace eng::ai

namespace eng::util {
template <>
struct Hash<eng::ai::numeric_detail::NumericKey> {
	[[nodiscard]] constexpr u32 operator()(
	    const eng::ai::numeric_detail::NumericKey& k) const noexcept {
		return hash_u32(k.facts ^ rotl(hash_u32(k.vars), 16u));
	}
};
} // namespace eng::util
