#pragma once

/// \file goap.hpp
/// **Planificacion GOAP** (Goal-Oriented Action Planning, `eng::ai`): cabecera **unica** de
/// la familia A*-GOAP. Un estado es un conjunto de **hechos booleanos** (`MaxFacts`) y, si
/// el dominio lo pide, hasta `MaxVars` **variables de nivel** (`u8`, 0..255): los enteros
/// son niveles directos y los decimales, niveles escalados (p. ej. `Fixed` q4.4 = nivel/16).
/// El agente describe acciones como **precondiciones + efectos + coste** y el planificador
/// busca con A* la secuencia de minimo coste.
///
/// Referencia de la tecnica: A. Alex, "Using GOAP for Advanced Gaming AI Techniques"
/// (https://arnauld-alex.com/using-goap-for-advanced-gaming-ai-techniques), basada en el
/// planificador de F.E.A.R. (Orkin). La implementacion es propia, freestanding y
/// determinista: sin `malloc`, sin excepciones y con todo el estado inline o en `scratch`
/// del llamador.
///
/// ## Modelo
///
/// Todo cuelga de un **dominio** `Goap<MaxFacts, MaxVars>`, que se escribe una sola vez con
/// un alias y no repite la capacidad por el codigo:
///
///   using Ai    = eng::ai::Goap<>;              // 32 hechos, 0 variables (el caso normal)
///   using Big   = eng::ai::Goap<64>;            // 64 hechos (clave de 64 bits)
///   using Needs = eng::ai::Goap<32, 2>;         // 32 hechos + 2 magnitudes (hambre, energia)
///
/// - `State`: `MaxFacts` hechos (`BitSet`) + `MaxVars` niveles. Cada hecho es un indice
///   (`Fact`); su significado (p. ej. "disk1_en_C", "horno_caliente") lo decide el juego.
/// - `Action`: booleanos (`require`/`forbid`/`produce`/`consume`) y numericos
///   (`var_ge`/`var_le` precondiciones; `add`/`set_var` efectos, saturados a 0..255), `cost`.
/// - `Goal`: hechos a 1 (`want_true`) y a 0 (`want_false`) + umbrales (`var_ge`, `var_le`).
/// - `Planner<MaxNodes>`: A* hacia delante. Heuristica de **objetivos pendientes**
///   (`plan`) o **grafo relajado** con memo (`plan_relaxed`). Caches: `plan_cached` (memo
///   por `(estado, objetivo)`) y `plan_reusing` (sufijo del plan anterior). **Anytime**:
///   `set_budget`/`partial()` (mejor plan parcial si se agota el presupuesto). Invalidacion
///   selectiva de la cache (`invalidate_selective`).
///
/// Consulta y aplicacion: `applicable(s, a)`, `apply(s, a)`, `satisfies(s, g)` y
/// `goal_distance(s, g)`.
///
/// ## Ancho de la clave
///
/// `MaxFacts` admite **32** (por defecto) o **64** (dos palabras, sin `long long`: el 68000
/// no tiene aritmetica de 64 bits y acabaria en libcalls). Las **variables** solo se admiten
/// con `MaxFacts <= 32`: hasta 4 la clave es exacta (`u32` hechos + `u32` niveles) y de 5 a 8
/// pasa a **ancha** (`StateKeyNVWide<2>`). Con `MaxVars == 0` se eliden del estado (0 bytes).
///
/// ## Coste y limites (68000)
///
/// Pensado para **`init` o tareas de fondo**, no para el camino por frame: el `Planner`
/// reserva un `HashMap` y una `PriorityQueue` de capacidad `MaxNodes` mas los nodos
/// generados. Instanciarlo en memoria estatica (no en la pila) y dimensionar `MaxNodes` por
/// escenario; `expansions()` dice cuanto trabajo costo el ultimo plan. Si el espacio no cabe
/// en `MaxNodes`, `plan()` devuelve `0` y `found()` queda a `false` (nunca corrompe ni
/// aborta); con `set_budget` devuelve el mejor parcial.
///
/// Verificacion: HOST-107 (Hanoi, pastel, soldado y dominio de 64 hechos), HOST-185/186
/// (numerico: enteros, decimales, saturacion, memo, sufijo y heuristica relajada), HOST-320
/// (anytime), HOST-321/316 (invalidacion selectiva y LRU).

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
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

/// Techo de variables de nivel por dominio: 4 con clave exacta de 64 bits, hasta 8 con la
/// clave ancha (`StateKeyNVWide<2>`).
inline constexpr usize goap_max_vars = 8u;

/// Indice invalido (raiz del arbol de busqueda, sin padre o sin accion).
inline constexpr u16 no_fact_link = 0xffffu;

namespace detail {

/// Seleccion perezosa de tipo (`std::conditional` sin STL).
template <bool C, class A, class B>
struct Cond {
	using type = A;
};
template <class A, class B>
struct Cond<false, A, B> {
	using type = B;
};

/// Clave de 64 bits en dos palabras de 32 (sin aritmetica de 64 bits en el 68000).
struct StateKey64 {
	u32 lo = 0u;
	u32 hi = 0u;
	[[nodiscard]] constexpr bool operator==(const StateKey64& other) const noexcept {
		return lo == other.lo && hi == other.hi;
	}
};

/// Clave exacta de hechos (<=32) + niveles: `u32` hechos + `u32` niveles empaquetados.
struct StateKeyNV {
	u32 facts = 0u;
	u32 vars = 0u;
	[[nodiscard]] constexpr bool operator==(const StateKeyNV& other) const noexcept {
		return facts == other.facts && vars == other.vars;
	}
};

/// Clave **ancha** de hechos (<=32) + niveles: `u32` hechos + `Words` palabras de niveles
/// (para `MaxVars` 5..8, que no caben en una sola palabra).
template <usize Words>
struct StateKeyNVWide {
	u32 facts = 0u;
	u32 vars[Words] {};
	[[nodiscard]] constexpr bool operator==(const StateKeyNVWide& other) const noexcept {
		if (facts != other.facts) {
			return false;
		}
		for (usize i = 0u; i < Words; ++i) {
			if (vars[i] != other.vars[i]) {
				return false;
			}
		}
		return true;
	}
};

/// Ancho de la clave segun `MaxFacts`/`MaxVars`.
template <usize MaxFacts, usize MaxVars>
struct KeyOf {
	static constexpr bool wide = (MaxFacts > 32u);
	static constexpr bool with_vars = (MaxVars > 0u);
	using vars_key = typename Cond<(MaxVars > 4u), StateKeyNVWide<2>, StateKeyNV>::type;
	using type = typename Cond<wide, StateKey64,
				   typename Cond<with_vars, vars_key, u32>::type>::type;
};

/// "Sin valor" para `var_le` (sin cota superior) y `var_set` (sin asignacion).
inline constexpr eng::u8 var_no_max = 0xffu;
/// "Sin efecto" en `add`/`sub`.
inline constexpr eng::s8 var_no_delta = 0;

/// Etiquetas que dan **tipos distintos** a cada juego de variables: dos miembros vacios de
/// distinto tipo si pueden solaparse con `[[no_unique_address]]` (los del mismo tipo no).
struct TagState {};
struct TagMin {};
struct TagMax {};
struct TagAdd {};
struct TagSet {};
struct TagGe {};
struct TagLe {};

/// Celdas indexables de `N`, **vacias** si `N == 0` (se eliden con `[[no_unique_address]]`).
template <class T, usize N, class Tag = void>
struct VarCells {
	T v[N] {};
	[[nodiscard]] constexpr T& operator[](usize i) noexcept { return v[i]; }
	[[nodiscard]] constexpr const T& operator[](usize i) const noexcept { return v[i]; }
};
template <class T, class Tag>
struct VarCells<T, 0u, Tag> {};

/// Satura un valor a nivel 0..255.
[[nodiscard]] constexpr eng::u8 sat_level(int value) noexcept {
	if (value < 0) {
		return 0u;
	}
	if (value > 255) {
		return 255u;
	}
	return static_cast<eng::u8>(value);
}

/// Memo de heuristica con capacidad `Cap`; con `Cap == 0` es **vacio** (no ocupa RAM), de
/// modo que un dominio sin heuristicas relajadas no paga por el (`Goap<32,0>`).
template <class K, usize Cap>
struct HMemo {
	eng::util::HashMap<K, u16, Cap> m {};
	[[nodiscard]] constexpr const u16* find(const K& k) const noexcept { return m.find(k); }
	constexpr void insert_or_assign(const K& k, u16 h) noexcept { m.insert_or_assign(k, h); }
	constexpr void clear() noexcept { m.clear(); }
};
template <class K>
struct HMemo<K, 0u> {
	[[nodiscard]] constexpr const u16* find(const K&) const noexcept { return nullptr; }
	constexpr void insert_or_assign(const K&, u16) noexcept {}
	constexpr void clear() noexcept {}
};

/// Estado: hechos booleanos + `MaxVars` niveles (0..255).
template <usize MaxFacts, usize MaxVars = 0u>
struct State {
	static_assert(MaxFacts == 32u || MaxFacts == 64u,
		      "State: MaxFacts debe ser 32 (clave u32) o 64 (clave de 64 bits)");
	static_assert(MaxFacts <= 32u || MaxVars == 0u,
		      "State: variables solo con MaxFacts <= 32");

	static constexpr bool wide = (MaxFacts > 32u);
	using Key = typename KeyOf<MaxFacts, MaxVars>::type;
	using Word = typename eng::util::BitSet<MaxFacts>::word_type;

	eng::util::BitSet<MaxFacts> facts {};
	[[no_unique_address]] VarCells<eng::u8, MaxVars, TagState> vars {};

	// Hechos: se leen y escriben por `facts` (un `BitSet<MaxFacts>`), igual que las mascaras
	// de `Action` y `Goal` — convencion unica en todo el dominio.
	// Variables: `var(i)` / `set_var(i, nivel)`.
	[[nodiscard]] constexpr eng::u8 var(usize i) const noexcept { return vars[i]; }
	constexpr void set_var(usize i, eng::u8 level) noexcept { vars[i] = level; }

	/// Clave densa del estado (`u32`, `StateKey64` o `StateKeyNV`).
	[[nodiscard]] constexpr Key key() const noexcept {
		if constexpr (MaxVars > 4u) {
			Key k {};
			k.facts = static_cast<u32>(facts.words()[0]);
			for (usize i = 0u; i < MaxVars; ++i) {
				k.vars[i / 4u] |= static_cast<u32>(vars[i])
						  << (8u * static_cast<u32>(i % 4u));
			}
			return k;
		} else if constexpr (MaxVars > 0u) {
			StateKeyNV k {};
			k.facts = static_cast<u32>(facts.words()[0]);
			u32 packed = 0u;
			for (usize i = 0u; i < MaxVars; ++i) {
				packed |= static_cast<u32>(vars[i]) << (8u * static_cast<u32>(i));
			}
			k.vars = packed;
			return k;
		} else if constexpr (wide) {
			return StateKey64 {static_cast<u32>(facts.words()[0]),
					   static_cast<u32>(facts.words()[1])};
		} else {
			return static_cast<u32>(facts.words()[0]);
		}
	}

	/// Reconstruye el estado a partir de su clave (inverso de `key()`).
	[[nodiscard]] static constexpr State from_key(Key k) noexcept {
		State s {};
		if constexpr (MaxVars > 4u) {
			s.facts.words()[0] = static_cast<Word>(k.facts);
			for (usize i = 0u; i < MaxVars; ++i) {
				s.vars[i] = static_cast<eng::u8>(
				    (k.vars[i / 4u] >> (8u * static_cast<u32>(i % 4u))) & 0xffu);
			}
		} else if constexpr (MaxVars > 0u) {
			s.facts.words()[0] = static_cast<Word>(k.facts);
			for (usize i = 0u; i < MaxVars; ++i) {
				s.vars[i] =
				    static_cast<eng::u8>((k.vars >> (8u * static_cast<u32>(i))) & 0xffu);
			}
		} else if constexpr (wide) {
			s.facts.words()[0] = static_cast<Word>(k.lo);
			s.facts.words()[1] = static_cast<Word>(k.hi);
		} else {
			s.facts.words()[0] = static_cast<Word>(k);
		}
		return s;
	}
};

/// Accion GOAP: que exige y que cambia del mundo, con su coste.
template <usize MaxFacts, usize MaxVars = 0u>
struct Action {
	State<MaxFacts> pre_true {};  ///< hechos que deben estar a 1
	State<MaxFacts> pre_false {}; ///< hechos que deben estar a 0
	State<MaxFacts> eff_add {};   ///< hechos que la accion pone a 1
	State<MaxFacts> eff_del {};   ///< hechos que la accion pone a 0

	[[no_unique_address]] VarCells<eng::u8, MaxVars, TagMin> var_min {}; ///< pre: var >= var_min
	[[no_unique_address]] VarCells<eng::u8, MaxVars, TagMax> var_max {}; ///< pre: var <= var_max
	[[no_unique_address]] VarCells<eng::s8, MaxVars, TagAdd> var_add {}; ///< efecto: var += add
	[[no_unique_address]] VarCells<eng::u8, MaxVars, TagSet> var_set {}; ///< efecto: var = set

	u16 cost = 1u;
	const char* name = nullptr; ///< solo diagnostico/host; puede ser `nullptr`

	/// Accion con valores por defecto: sin cota superior de variable y sin `set`.
	constexpr Action() noexcept {
		if constexpr (MaxVars > 0u) {
			for (usize i = 0u; i < MaxVars; ++i) {
				var_max[i] = var_no_max;
				var_set[i] = var_no_max;
			}
		}
	}
};

/// Objetivo: hechos a 1 / a 0 + umbrales numericos.
template <usize MaxFacts, usize MaxVars = 0u>
struct Goal {
	State<MaxFacts> want_true {};
	State<MaxFacts> want_false {};
	[[no_unique_address]] VarCells<eng::u8, MaxVars, TagGe> var_ge {}; ///< exigido var >= var_ge
	[[no_unique_address]] VarCells<eng::u8, MaxVars, TagLe> var_le {}; ///< exigido var <= var_le

	/// Objetivo sin umbrales de variable (por defecto: sin cota superior).
	constexpr Goal() noexcept {
		if constexpr (MaxVars > 0u) {
			for (usize i = 0u; i < MaxVars; ++i) {
				var_le[i] = var_no_max;
			}
		}
	}
};

/// Dominio GOAP: agrupa las acciones y el objetivo de un problema concreto.
template <usize MaxFacts, usize MaxVars, usize MaxActions>
struct Domain {
	eng::util::Array<Action<MaxFacts, MaxVars>, MaxActions> actions {};
	Goal<MaxFacts, MaxVars> goal {};

	[[nodiscard]] constexpr Span<const Action<MaxFacts, MaxVars>> action_span() const noexcept {
		return actions.span();
	}
};

/// Constructor fluido de acciones y objetivos (se usa en `constexpr`).
template <usize MaxFacts, usize MaxVars = 0u>
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
	/// Precondicion `var >= min`.
	[[nodiscard]] constexpr Builder& var_ge(usize v, eng::u8 min) noexcept {
		m_action.var_min[v] = min;
		return *this;
	}
	/// Precondicion `var <= max`.
	[[nodiscard]] constexpr Builder& var_le(usize v, eng::u8 max) noexcept {
		m_action.var_max[v] = max;
		return *this;
	}
	/// Efecto `var += delta` (saturado a 0..255; negativo resta).
	[[nodiscard]] constexpr Builder& add(usize v, int delta) noexcept {
		m_action.var_add[v] = static_cast<eng::s8>(delta);
		return *this;
	}
	/// Efecto `var = level` (sustituye a `add` sobre esa variable).
	[[nodiscard]] constexpr Builder& set_var(usize v, eng::u8 level) noexcept {
		m_action.var_set[v] = level;
		return *this;
	}
	[[nodiscard]] constexpr Action<MaxFacts, MaxVars> build() const noexcept { return m_action; }

private:
	Action<MaxFacts, MaxVars> m_action {};
};

/// ¿Se cumplen las precondiciones de `a` en `s`? (por palabras; hechos y niveles).
template <usize MaxFacts, usize MaxVars>
[[nodiscard]] constexpr bool applicable(const State<MaxFacts, MaxVars>& s,
					const Action<MaxFacts, MaxVars>& a) noexcept {
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
	if constexpr (MaxVars > 0u) {
		for (usize i = 0; i < MaxVars; ++i) {
			if (a.var_min[i] != 0u && s.vars[i] < a.var_min[i]) {
				return false;
			}
			if (a.var_max[i] != var_no_max && s.vars[i] > a.var_max[i]) {
				return false;
			}
		}
	}
	return true;
}

/// Aplica los efectos de `a` sobre `s` (hechos: añade y luego elimina; niveles: `set` o
/// `add`, saturados). La accion debe ser `applicable`.
template <usize MaxFacts, usize MaxVars>
constexpr void apply(State<MaxFacts, MaxVars>& s, const Action<MaxFacts, MaxVars>& a) noexcept {
	using Word = typename eng::util::BitSet<MaxFacts>::word_type;
	constexpr usize W = eng::util::BitSet<MaxFacts>::word_count;
	for (usize w = 0; w < W; ++w) {
		s.facts.words()[w] = static_cast<Word>(
			(s.facts.words()[w] | a.eff_add.facts.words()[w]) & ~a.eff_del.facts.words()[w]);
	}
	if constexpr (MaxVars > 0u) {
		for (usize i = 0; i < MaxVars; ++i) {
			if (a.var_set[i] != var_no_max) {
				s.vars[i] = a.var_set[i];
			} else if (a.var_add[i] != var_no_delta) {
				s.vars[i] = sat_level(static_cast<int>(s.vars[i]) + a.var_add[i]);
			}
		}
	}
}

/// ¿Cumple `s` el objetivo `g`?
template <usize MaxFacts, usize MaxVars>
[[nodiscard]] constexpr bool satisfies(const State<MaxFacts, MaxVars>& s,
				       const Goal<MaxFacts, MaxVars>& g) noexcept {
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
	if constexpr (MaxVars > 0u) {
		for (usize i = 0; i < MaxVars; ++i) {
			if (g.var_ge[i] != 0u && s.vars[i] < g.var_ge[i]) {
				return false;
			}
			if (g.var_le[i] != var_no_max && s.vars[i] > g.var_le[i]) {
				return false;
			}
		}
	}
	return true;
}

/// Heuristica: hechos del objetivo pendientes + variables fuera de umbral.
template <usize MaxFacts, usize MaxVars>
[[nodiscard]] constexpr u16 goal_distance(const State<MaxFacts, MaxVars>& s,
					  const Goal<MaxFacts, MaxVars>& g) noexcept {
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
	if constexpr (MaxVars > 0u) {
		for (usize i = 0; i < MaxVars; ++i) {
			if (g.var_ge[i] != 0u && s.vars[i] < g.var_ge[i]) {
				++d;
			}
			if (g.var_le[i] != var_no_max && s.vars[i] > g.var_le[i]) {
				++d;
			}
		}
	}
	return d;
}

/// Planificador GOAP: A* hacia delante sobre el espacio de hechos (y niveles).
///
/// `MaxNodes` es el presupuesto comun de nodos generados, entradas del mapa de mejor coste
/// y elementos de la cola. El estado de trabajo va inline en el objeto, asi que conviene
/// instanciarlo en estatica para no consumir pila en el 68000.
template <usize MaxFacts, usize MaxVars, usize MaxNodes, usize MaxCachedPlans = 4u,
	  usize MaxCachedActions = 64u>
class Planner {
	static_assert(MaxNodes > 0u, "Planner: MaxNodes debe ser mayor que 0");

public:
	using StateT = State<MaxFacts, MaxVars>;
	using ActionT = Action<MaxFacts, MaxVars>;
	using GoalT = Goal<MaxFacts, MaxVars>;
	using KeyT = typename StateT::Key;

	/// ¿La ultima llamada encontro una solucion? (un plan vacio es valido: el estado
	/// inicial ya cumplia el objetivo).
	[[nodiscard]] constexpr bool found() const noexcept { return m_found; }

	/// Coste total del ultimo plan (0 si vacio o no encontrado).
	[[nodiscard]] constexpr u16 plan_cost() const noexcept { return m_cost; }

	/// Nodos expandidos en la ultima busqueda (diagnostico de presupuesto).
	[[nodiscard]] constexpr usize expansions() const noexcept { return m_expansions; }

	/// Fija el **presupuesto** de la busqueda: maximo de nodos a expandir (0 = sin limite).
	/// Con presupuesto, `plan()` devuelve el mejor **parcial** si no alcanza el objetivo
	/// (ver `partial()`); pensado para reparto de CPU por tick en el 68000.
	constexpr void set_budget(usize max_expansions) noexcept {
		m_max_expansions = max_expansions;
	}
	[[nodiscard]] constexpr usize budget() const noexcept { return m_max_expansions; }

	/// ¿El ultimo plan es **parcial**? (el presupuesto corto la busqueda sin alcanzar el
	/// objetivo). El parcial es el mejor nodo visitado (menor `h`; a igual `h`, mayor avance
	/// `g`), no se cachea, y el llamador decide si lo ejecuta o espera mas presupuesto.
	[[nodiscard]] constexpr bool partial() const noexcept { return m_partial; }

	/// Busca el plan de coste minimo de `start` a `goal` (heuristica de objetivos
	/// pendientes) y escribe los indices de accion en `out`. Devuelve el numero de acciones
	/// (0 si vacio o no encontrado; ver `found()`/`partial()`).
	[[nodiscard]] constexpr usize plan(const StateT& start, const GoalT& goal,
					   Span<const ActionT> actions, Span<u16> out) noexcept {
		return plan_impl<false>(start, goal, actions, out);
	}

	/// Como `plan`, pero con **heuristica de grafo relajado** (`h_max` sobre hechos +
	/// cota numerica) y **memo** entre llamadas. Guia mejor; no garantiza optimalidad.
	[[nodiscard]] constexpr usize plan_relaxed(const StateT& start, const GoalT& goal,
						   Span<const ActionT> actions, Span<u16> out) noexcept {
		return plan_impl<true>(start, goal, actions, out);
	}

	[[nodiscard]] constexpr usize heuristic_hits() const noexcept { return m_h_hits; }

	/// Vacia el memo de la heuristica relajada.
	constexpr void clear_heuristic_cache() noexcept {
		m_h_cache.clear();
		m_h_hits = 0u;
	}

	/// Variante que toma un `Domain` (acciones + objetivo) en una sola pieza.
	template <usize MaxActions>
	[[nodiscard]] constexpr usize plan(const StateT& start,
					   const Domain<MaxFacts, MaxVars, MaxActions>& domain,
					   Span<u16> out) noexcept {
		return plan(start, domain.goal, domain.action_span(), out);
	}

	/// Como `plan`, pero **cachea** el ultimo resultado por `(estado, objetivo)`: si la
	/// misma consulta se repite, devuelve el plan guardado sin buscar. El parcial no se
	/// cachea. `clear_plan_cache()` vacia; `invalidate_selective` descarta por dependencias.
	[[nodiscard]] constexpr usize plan_cached(const StateT& start, const GoalT& goal,
						  Span<const ActionT> actions,
						  Span<u16> out) noexcept {
		const KeyT k_start = start.key();
		const GoalKey gk = goal_key(goal);
		for (usize i = 0; i < m_cache_count; ++i) {
			CacheEntry& e = m_cache[i];
			if (e.start == k_start && e.goal == gk) {
				if (e.length > out.size()) {
					break;
				}
				for (usize j = 0; j < e.length; ++j) {
					out[j] = m_plan_pool[e.offset + j];
				}
				++e.hits;
				m_found = true;
				m_cost = e.cost;
				m_partial = false;
				m_expansions = 0u;
				return e.length;
			}
		}
		const usize n = plan(start, goal, actions, out);
		if (m_found && m_plan_used + n <= MaxCachedActions) {
			if (m_cache_count >= MaxCachedPlans) {
				drop_lru(); // politica LRU+menos-usos
			}
			if (m_cache_count < MaxCachedPlans) {
				CacheEntry& e = m_cache[m_cache_count++];
				e.start = k_start;
				e.goal = gk;
				e.hits = 0u;
				e.used_facts = used_facts_of(actions, out, n);
				e.used_vars = used_vars_of(actions, out, n);
				e.offset = m_plan_used;
				e.length = static_cast<u16>(n);
				e.cost = m_cost;
				for (usize j = 0; j < n; ++j) {
					m_plan_pool[m_plan_used + j] = out[j];
				}
				m_plan_used = static_cast<u16>(m_plan_used + n);
			}
		}
		return n;
	}

	/// Reutiliza el **sufijo** del plan anterior tras ejecutar el paso `taken`: si el resto
	/// sigue aplicandose desde `start` y cumple el objetivo, lo devuelve sin buscar; si no,
	/// replantea con `plan_cached`.
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
			m_partial = false;
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

	/// Invalida solo las entradas cuyo plan **depende** de lo cambiado (hechos o variables):
	/// conserva las demas y compacta el pool. `clear_plan_cache()` es el vaciado total.
	constexpr void invalidate_selective(const StateT& changed) noexcept {
		usize keep = 0u;
		u16 pool_at = 0u;
		for (usize i = 0u; i < m_cache_count; ++i) {
			const CacheEntry& e = m_cache[i];
			using Word = typename eng::util::BitSet<MaxFacts>::word_type;
			constexpr usize W = eng::util::BitSet<MaxFacts>::word_count;
			bool affects = false;
			for (usize w = 0u; w < W; ++w) {
				if ((e.used_facts.words()[w] & changed.facts.words()[w]) !=
				    static_cast<Word>(0)) {
					affects = true;
					break;
				}
			}
			if constexpr (MaxVars > 0u) {
				for (usize v = 0u; !affects && v < MaxVars; ++v) {
					if ((e.used_vars & static_cast<eng::u8>(1u << v)) != 0u &&
					    changed.vars[v] != 0u) {
						affects = true;
					}
				}
			}
			if (affects) {
				continue; // dependencia cambiada: se descarta
			}
			CacheEntry moved = e;
			moved.offset = pool_at;
			for (u16 j = 0u; j < e.length; ++j) {
				m_plan_pool[pool_at + j] = m_plan_pool[e.offset + j];
			}
			m_cache[keep] = moved;
			pool_at = static_cast<u16>(pool_at + e.length);
			++keep;
		}
		m_cache_count = keep;
		m_plan_used = pool_at;
	}

	[[nodiscard]] constexpr usize cache_size() const noexcept { return m_cache_count; }

private:
	/// Nucleo de la busqueda A*: respeta `set_budget` y devuelve el mejor parcial si el
	/// presupuesto corta la busqueda. `Relaxed` elige la heuristica.
	template <bool Relaxed>
	[[nodiscard]] constexpr usize plan_impl(const StateT& start, const GoalT& goal,
						Span<const ActionT> actions, Span<u16> out) noexcept {
		m_found = false;
		m_partial = false;
		m_cost = 0u;
		m_expansions = 0u;
		m_node_count = 0u;
		m_best_partial_node = no_fact_link;
		m_best_partial_h = 0xffffu;
		m_best_partial_g = 0u;
		m_best.clear();
		m_open.clear();

		if (satisfies(start, goal)) {
			m_found = true;
			return 0u;
		}
		if (actions.empty() || out.empty()) {
			return 0u;
		}

		const u16 root_h = heuristic<Relaxed>(start, goal, actions);
		const u16 root = add_node(start.key(), no_fact_link, no_fact_link, 0u);
		m_best.insert(start.key(), 0u);
		m_open.push(OpenNode {start.key(), 0u, root_h, root});
		m_best_partial_h = root_h;

		u16 goal_node = no_fact_link;
		// Un parcial solo es valido si el **presupuesto** corto la busqueda.
		bool budget_stopped = false;
		while (!m_open.empty()) {
			if (m_max_expansions != 0u && m_expansions >= m_max_expansions) {
				budget_stopped = true;
				break;
			}
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
				const u16 h = heuristic<Relaxed>(next, goal, actions);
				const u16 fc = static_cast<u16>(static_cast<u32>(ng) + h > 0xffffu
								    ? 0xffffu
								    : static_cast<u32>(ng) + h);
				if (h < m_best_partial_h ||
				    (h == m_best_partial_h && ng > m_best_partial_g)) {
					m_best_partial_h = h;
					m_best_partial_node = child;
					m_best_partial_g = ng;
				}
				if (!m_open.push(OpenNode {next.key(), ng, fc, child})) {
					return 0u; // cola llena: no hay hueco para mas candidatos
				}
			}
		}

		if (goal_node == no_fact_link) {
			if (budget_stopped && m_best_partial_node != no_fact_link) {
				m_partial = true;
				m_cost = m_best_partial_g;
				return reconstruct(m_best_partial_node, out);
			}
			return 0u;
		}
		m_found = true;
		return reconstruct(goal_node, out);
	}

	/// Distancia al objetivo: relajada (`Relaxed`) o de objetivos pendientes.
	template <bool Relaxed>
	[[nodiscard]] constexpr u16 heuristic(const StateT& s, const GoalT& g,
					      Span<const ActionT> actions) noexcept {
		if constexpr (Relaxed) {
			return relaxed_distance(s, g, actions);
		} else {
			(void)actions;
			return goal_distance(s, g);
		}
	}

	/// Heuristica de grafo relajado: h_max sobre hechos (ignorando borrados) + cota
	/// numerica por el mayor delta por accion. Con memo por clave de estado.
	[[nodiscard]] constexpr u16 relaxed_distance(const StateT& s, const GoalT& g,
						     Span<const ActionT> actions) noexcept {
		const KeyT k = s.key();
		if (const u16* cached = m_h_cache.find(k)) {
			++m_h_hits;
			return *cached;
		}
		constexpr u16 inf = 0xffffu;
		u16 fact_cost[MaxFacts];
		for (usize f = 0; f < MaxFacts; ++f) {
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
				for (usize f = 0; f < MaxFacts; ++f) {
					if (a.pre_true.facts.test(f)) {
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
				for (usize f = 0; f < MaxFacts; ++f) {
					if (a.eff_add.facts.test(f)) {
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
		for (usize f = 0; f < MaxFacts; ++f) {
			if (g.want_true.facts.test(f)) {
				h = static_cast<u16>(h + (fact_cost[f] == inf ? 255u : fact_cost[f]));
			}
			if (g.want_false.facts.test(f) && s.facts.test(f)) {
				h = static_cast<u16>(h + 1u);
			}
		}
		if constexpr (MaxVars > 0u) {
			for (usize i = 0; i < MaxVars; ++i) {
				if (g.var_ge[i] != 0u && s.vars[i] < g.var_ge[i]) {
					int best_delta = 0;
					u16 min_cost = inf;
					for (usize ai = 0; ai < actions.size(); ++ai) {
						const int d = actions[ai].var_add[i];
						if (d > best_delta) {
							best_delta = d;
							min_cost = actions[ai].cost;
						} else if (d == best_delta && d > 0 &&
							   actions[ai].cost < min_cost) {
							min_cost = actions[ai].cost;
						}
					}
					const int needed = static_cast<int>(g.var_ge[i]) -
							   static_cast<int>(s.vars[i]);
					if (best_delta <= 0) {
						h = static_cast<u16>(h + 255u);
					} else {
						const u16 steps = static_cast<u16>(
						    (needed + best_delta - 1) / best_delta);
						h = static_cast<u16>(
						    h + steps * (min_cost == inf ? 1u : min_cost));
					}
				}
			}
		}
		m_h_cache.insert_or_assign(k, h);
		return h;
	}

	/// Clave exacta del objetivo (hechos + umbrales) para la cache de planes.
	struct GoalKey {
		using FKey = typename State<MaxFacts>::Key;
		FKey want_true {};
		FKey want_false {};
		[[no_unique_address]] VarCells<eng::u8, MaxVars, TagGe> var_ge {};
		[[no_unique_address]] VarCells<eng::u8, MaxVars, TagLe> var_le {};
		[[nodiscard]] constexpr bool operator==(const GoalKey& other) const noexcept {
			if (!(want_true == other.want_true) ||
			    !(want_false == other.want_false)) {
				return false;
			}
			if constexpr (MaxVars > 0u) {
				for (usize i = 0u; i < MaxVars; ++i) {
					if (var_ge[i] != other.var_ge[i] ||
					    var_le[i] != other.var_le[i]) {
						return false;
					}
				}
			}
			return true;
		}
	};

	/// Clave exacta del objetivo (hechos + umbrales) para la cache de planes.
	[[nodiscard]] static constexpr GoalKey goal_key(const GoalT& g) noexcept {
		GoalKey k {};
		k.want_true = g.want_true.key();
		k.want_false = g.want_false.key();
		if constexpr (MaxVars > 0u) {
			for (usize i = 0u; i < MaxVars; ++i) {
				k.var_ge[i] = g.var_ge[i];
				k.var_le[i] = g.var_le[i];
			}
		}
		return k;
	}

	/// Hechos que el plan requiere o modifica (union de las acciones).
	[[nodiscard]] static constexpr eng::util::BitSet<MaxFacts> used_facts_of(
	    Span<const ActionT> actions, Span<const u16> plan, usize n) noexcept {
		using Word = typename eng::util::BitSet<MaxFacts>::word_type;
		constexpr usize W = eng::util::BitSet<MaxFacts>::word_count;
		eng::util::BitSet<MaxFacts> used {};
		for (usize j = 0u; j < n; ++j) {
			const ActionT& a = actions[plan[j]];
			for (usize w = 0u; w < W; ++w) {
				used.words()[w] = static_cast<Word>(
				    used.words()[w] | a.pre_true.facts.words()[w] |
				    a.pre_false.facts.words()[w] | a.eff_add.facts.words()[w] |
				    a.eff_del.facts.words()[w]);
			}
		}
		return used;
	}

	/// Mascara de variables que intervienen en el plan (bit i = variable i).
	[[nodiscard]] static constexpr eng::u8 used_vars_of(Span<const ActionT> actions,
							    Span<const u16> plan,
							    usize n) noexcept {
		eng::u8 mask = 0u;
		if constexpr (MaxVars > 0u) {
			for (usize j = 0u; j < n; ++j) {
				const ActionT& a = actions[plan[j]];
				for (usize i = 0u; i < MaxVars; ++i) {
					if (a.var_min[i] != 0u || a.var_max[i] != var_no_max ||
					    a.var_add[i] != var_no_delta ||
					    a.var_set[i] != var_no_max) {
						mask = static_cast<eng::u8>(mask | (1u << i));
					}
				}
			}
		}
		return mask;
	}

	/// Desaloja la entrada con menos usos (`hits`) y recompacta el pool (LRU+menos-usos).
	constexpr void drop_lru() noexcept {
		if (m_cache_count == 0u) {
			return;
		}
		usize victim = 0u;
		for (usize i = 1u; i < m_cache_count; ++i) {
			if (m_cache[i].hits < m_cache[victim].hits) {
				victim = i;
			}
		}
		for (usize i = victim; i + 1u < m_cache_count; ++i) {
			m_cache[i] = m_cache[i + 1u];
		}
		--m_cache_count;
		u16 at = 0u;
		for (usize i = 0u; i < m_cache_count; ++i) {
			CacheEntry& e = m_cache[i];
			for (u16 j = 0u; j < e.length; ++j) {
				m_plan_pool[at + j] = m_plan_pool[e.offset + j];
			}
			e.offset = at;
			at = static_cast<u16>(at + e.length);
		}
		m_plan_used = at;
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

	/// Entrada de la cache de planes (`plan_cached`).
	struct CacheEntry {
		KeyT start {};
		GoalKey goal {};
		/// Dependencias del plan (invalidacion selectiva).
		eng::util::BitSet<MaxFacts> used_facts {};
		eng::u8 used_vars = 0u;
		u16 hits = 0u;
		u16 offset = 0u;
		u16 length = 0u;
		u16 cost = 0u;
	};

	eng::util::HashMap<KeyT, u16, MaxNodes> m_best {}; ///< estado -> mejor coste g
	/// Memo de la heuristica relajada (solo si el dominio tiene variables; 0 bytes si no).
	HMemo<KeyT, (MaxVars > 0u ? MaxNodes : 0u)> m_h_cache {};
	eng::util::PriorityQueue<OpenNode, MaxNodes, OpenCmp> m_open {};
	Node m_nodes[MaxNodes] {};
	usize m_node_count = 0u;
	usize m_expansions = 0u;
	usize m_max_expansions = 0u; ///< 0 = sin limite (ver `set_budget`)
	usize m_h_hits = 0u;
	u16 m_best_partial_node = no_fact_link;
	u16 m_best_partial_h = 0xffffu;
	u16 m_best_partial_g = 0u;
	u16 m_cost = 0u;
	bool m_found = false;
	bool m_partial = false;

	CacheEntry m_cache[MaxCachedPlans] {};
	usize m_cache_count = 0u;
	u16 m_plan_pool[MaxCachedActions] {};
	u16 m_plan_used = 0u;
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

/// `HashMap` hashea la clave de hechos+niveles (`StateKeyNV`).
template <>
struct Hash<eng::ai::detail::StateKeyNV> {
	[[nodiscard]] constexpr u32 operator()(const eng::ai::detail::StateKeyNV& k) const noexcept {
		return hash_u32(k.facts ^ rotl(hash_u32(k.vars), 16u));
	}
};

/// `HashMap` hashea la clave ancha de hechos+niveles (`StateKeyNVWide`).
template <usize Words>
struct Hash<eng::ai::detail::StateKeyNVWide<Words>> {
	[[nodiscard]] constexpr u32 operator()(
	    const eng::ai::detail::StateKeyNVWide<Words>& k) const noexcept {
		u32 h = k.facts;
		for (usize i = 0u; i < Words; ++i) {
			h = hash_u32(h ^ rotl(hash_u32(k.vars[i]), 16u));
		}
		return h;
	}
};

} // namespace eng::util

namespace eng::ai {

using detail::applicable;
using detail::apply;
using detail::goal_distance;
using detail::satisfies;

/// Dominio GOAP: agrupa los tipos de un mismo conjunto de hechos y variables. Se escribe
/// una vez (`using Ai = eng::ai::Goap<>;`) y evita repetir la capacidad por el codigo.
///
/// `MaxFacts` admite `32` (clave `u32`) o `64` (clave de 64 bits en dos palabras). `MaxVars`
/// (0..4) solo es valido con `MaxFacts <= 32`; con 0 variables, estas se eliden del estado.
template <usize MaxFacts = 32u, usize MaxVars = 0u>
struct Goap {
	static_assert(MaxFacts == 32u || MaxFacts == 64u,
		      "Goap: MaxFacts debe ser 32 (clave u32) o 64 (clave de 64 bits)");
	static_assert(MaxFacts <= 32u || MaxVars == 0u,
		      "Goap: variables solo con MaxFacts <= 32");
	static_assert(MaxVars <= goap_max_vars, "Goap: MaxVars <= 4");

	using State = detail::State<MaxFacts, MaxVars>;
	using Action = detail::Action<MaxFacts, MaxVars>;
	using Builder = detail::Builder<MaxFacts, MaxVars>;
	using Goal = detail::Goal<MaxFacts, MaxVars>;

	template <usize MaxActions>
	using Domain = detail::Domain<MaxFacts, MaxVars, MaxActions>;

	template <usize MaxNodes = 128u, usize MaxCachedPlans = 4u, usize MaxCachedActions = 64u>
	using Planner = detail::Planner<MaxFacts, MaxVars, MaxNodes, MaxCachedPlans,
					MaxCachedActions>;

	/// Estado formado por los hechos indicados (azucar para construir escenarios).
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
