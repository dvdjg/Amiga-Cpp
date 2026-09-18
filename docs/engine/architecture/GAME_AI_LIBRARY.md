# Librería de IA de juego y técnicas de diseño (`eng::ai`)

`engine/include/eng/ai/` reúne los algoritmos de **IA clásica de videojuego** que el
engine puede ejecutar en el A500 sin `malloc`, sin excepciones y de forma determinista:
planificación de acciones, decisión, navegación, movimiento y percepción. Es la capa
de la «lógica de juego» (ver la separación de capas de [CODING_STYLE.md](CODING_STYLE.md)):
no conoce hardware, no incluye registros ni DMA y compila igual en host que en el
cruce `m68k`.

Las **técnicas de diseño de videojuego** (pacing, dificultad dinámica, recompensas,
diseño de niveles) son conocimiento y patrones, no código: su catálogo y su orden de
adopción viven en [ROADMAP_GAME_AI.md](../../guides/roadmap/ROADMAP_GAME_AI.md).
Cuando una de ellas necesite una pieza de engine (un director de dificultad, un
programador de recompensas), se implementa en `eng::ai::design` con consumidor real.

## 1. Encaje con lo que ya existe

`eng::ai` **complementa** `eng::util` y las matemáticas de `eng::math`; no las duplica:

```
   eng/core/ + eng/core/util/               eng/ai/  (esta librería)
   ──────────────────────────               ──────────────────────────────
   BitSet<N>        conjunto de bits  ◄──── WorldState  (hechos del mundo)
   HashMap<K,V,N>   tabla fija        ◄──── cierre de A* + deduplicación de estado
   PriorityQueue    heap fijo         ◄──── cola abierta del planner A*
   pathfinding.hpp  BFS/A* en rejilla       navigation/  (navmesh, flow field)
   random/noise     variación               decision/    (utilidad, comportamiento)
```

Puntos de reutilización explícitos:

- `WorldState` encapsula un `eng::util::BitSet<32>` (tope `max_facts`); el significado de
  cada hecho (`Fact`, un `u16`) lo fija el juego con un `enum`.
- El planificador usa `eng::util::HashMap` (estado → mejor coste, deduplicación `O(1)`)
  y `eng::util::PriorityQueue` (min-heap por `f`), ya verificados por HOST-083/089.
- La búsqueda en rejilla (`eng::util::bfs`/`astar`) es para mapas **densos**; la
  navegación sobre geometría/topología de `eng::ai::navigation` la usará como
  primitiva cuando exista.
- La máquina de estados genérica existe en `eng::util` (`core/util/state_machine.hpp`,
  HOST-108); `eng::ai::decision` se construye **sobre** ella (no se duplica una FSM).

## 2. Organización por familias

Cada familia de técnicas tiene su subcarpeta y su espacio de nombres (`eng::ai`), de
modo que añadir una técnica nueva no reordena nada:

```
engine/include/eng/ai/
├── planning/     → PLANIFICACIÓN de acciones (goap.hpp; futuro HTN, planificador jerárquico)
├── decision/     → DECISIÓN por tick (agent_fsm.hpp; futuro utility AI, behavior trees, selectores)
├── navigation/   → NAVEGACIÓN en el mapa (navmesh lite tipo Recast/Detour, flow fields)
├── steering/     → MOVIMIENTO continuo (seek/flee/arrive, flocking, evasión)
├── perception/   → MEMORIA y creencias (influence maps, blackboard, sensores, threat)
└── design/       → DISEÑO con consumidor (director de dificultad, recompensas, pacing)
```

La decisión se apoya en la FSM genérica de `eng::util` (`state_machine.hpp`, HOST-108) y en el
emisor de eventos (`event.hpp`, HOST-109): `AgentFsm` (en `decision/`) **contiene** la
`StateMachine` y añade los efectos de entrada/salida; no reimplementa transiciones.

```
   percepción          decisión            planificación
   ┌──────────┐       ┌──────────┐        ┌──────────────┐
   │ influence│──────►│ FSM /    │───────►│ GOAP         │
   │ blackboard│      │ utility  │        │ (plan de      │
   └──────────┘       │ behavior │        │  acciones)    │
        ▲             └────┬─────┘        └──────┬───────┘
        │                  ▼                     ▼
        │            navegación            steering (seguir
        └────────────(navmesh/flow)◄────── el plan / moverse)
```

## 3. Planificación: GOAP (`eng/ai/planning/goap.hpp`)

**GOAP** (Goal-Oriented Action Planning) describe al agente con **hechos booleanos** y
las acciones como **precondiciones + efectos + coste**; el planificador busca con A*
la secuencia de coste mínimo que lleva del estado actual al objetivo. Referencia de la
técnica: A. Alex, *Using GOAP for Advanced Gaming AI Techniques*
(<https://arnauld-alex.com/using-goap-for-advanced-gaming-ai-techniques>), basada en el
planificador de *F.E.A.R.* (Orkin). La implementación es propia, freestanding y sin
heap.

### 3.1 Modelo

Los tipos cuelgan de un **dominio** `Goap<MaxFacts>`, que se declara una sola vez con un
alias (`using Ai = eng::ai::Goap<>;`); así la capacidad se escribe una vez y no se repite
por el código.

| Tipo | Papel |
|---|---|
| `Fact` (`u16`) | índice de un hecho booleano; su significado lo fija el juego |
| `Ai::State` | `MaxFacts` hechos booleanos (`BitSet`); `key()` los empaqueta en `u32` (o en dos palabras si son 64) |
| `Ai::state(hechos…)` | construye un estado a partir de sus hechos (constantes o de runtime) |
| `Ai::Action` | `pre_true`, `pre_false`, `eff_add`, `eff_del`, `cost`, `name` |
| `Ai::Builder` | constructor fluido (`named`/`cost`/`require`/`forbid`/`produce`/`consume`) |
| `Ai::Goal` | `want_true` (hechos exigidos a 1) y `want_false` (exigidos a 0) |
| `Ai::goal(hechos…)` | construye un `Goal` con los hechos exigidos a 1 |
| `Ai::Domain<MaxActions>` | dominio listo: `actions` (`Array`) + `goal`; el planner lo acepta directo |
| `Ai::Planner<MaxNodes>` | A* hacia delante; `plan()`, `found()`, `plan_cost()`, `expansions()` |

`MaxFacts` solo admite dos valores: **32** (por defecto, clave `u32`) o **64** (clave de
64 bits empaquetada en dos palabras). No hay valores intermedios útiles: `BitSet<N>` ocupa
una sola palabra para todo `N ≤ 32` y el código generado es idéntico, así que lo único que
cambia de verdad es el ancho de la clave. El otro parámetro de la API es el presupuesto de
búsqueda del `Planner<MaxNodes>`, con valor por defecto. Consulta de estado:
`applicable(s, a)`, `apply(s, a)`, `satisfies(s, g)` y `goal_distance(s, g)`, que deducen
`MaxFacts` de sus argumentos.

### 3.2 Uso

```cpp
enum : eng::u16 { kHarina, kHuevos, kMezcla, kHorneado };
using Ai = eng::ai::Goap<>;                  // 32 hechos (el caso normal)
constexpr eng::util::Array<Ai::Action, 3> acciones { {
    Ai::Builder{}.named("comprar").produce(kHarina, kHuevos).build(),
    Ai::Builder{}.named("batir").require(kHarina, kHuevos).produce(kMezcla).build(),
    Ai::Builder{}.named("hornear").require(kMezcla).produce(kHorneado).build(),
} };
Ai::Goal meta;
meta.want_true.facts.set(kHorneado);

Ai::Planner<64> planner;            // en Amiga: instancia estatica, no de pila
eng::u16 plan[8];
const eng::usize n = planner.plan(Ai::state(), meta, acciones.span(),
                                  eng::Span<eng::u16> {plan, 8u});
```

`plan()` escribe en `plan` los índices de acción (de primero a último) y devuelve su
número. Un plan vacío con `found() == true` significa que el estado ya cumplía el
objetivo; `found() == false` con `0` significa que no hay solución (o no cabe en
`out`/`MaxNodes`).

Para no repetir `Array` + `Goal`, `Ai::Domain<MaxActions>` agrupa ambos y el planner tiene
una sobrecarga: `Ai::Domain<3> problema { { a0, a1, a2 }, Ai::goal(kMeta) };
planner.plan(start, problema, out);`. Ahorra un argumento y una declaración; compensa cuando
hay varios dominios o agentes, y aporta poco con uno solo.

### 3.3 Coste y límites (A500)

- El planificador usa `HashMap`, `PriorityQueue` y los nodos **inline** en el objeto
  `Ai::Planner`. `MaxNodes` dimensiona los tres: con la clave de 32 bits, `Ai::Planner<256>`
  ocupa **8280 B** (`Ai::Planner<128>`, 4152 B); con la de 64 bits, **12 376 B**
  (`Ai::Planner<128>`, 6200 B). Se instancia en **memoria estática** (no en la pila del
  68000) y se ejecuta en `init` o en una tarea de fondo (`eng::task`), nunca en el camino por
  frame. Tamaños medidos con el compilador cruzado (sonda `Show<sizeof(T)>`, `-mcpu=68000`):
  `State` 4 B (32 hechos) / 8 B (64), `Action` 22 B / 38 B.
- La clave de 64 bits se empaqueta en **dos `u32`** (`StateKey64`), no en un
  `unsigned long long`: el 68000 no tiene aritmética nativa de 64 bits y `long long` acabaría
  en libcalls (`__ashldi3`, `__lshrdi3`). El hash combina las dos palabras con `hash_u32` y el
  gate de codegen (`c_goap64_ops`) comprueba que esas libcalls no aparecen. Un índice de hecho
  fuera del rango del dominio dispara `illegal` (contrato de `BitSet`).
- La heurística es el número de hechos del objetivo pendientes. Es **admisible cuando
  cada acción satisface como mucho un hecho del objetivo** (el caso de estos planes); si
  una acción resolviera varios hechos de golpe, la heurística puede sobreestimar y el
  plan deja de estar garantizado como óptimo (sigue siendo válido). Con un único hecho
  objetivo degenera en búsqueda de anchura.
- Agotar `MaxNodes` no corrompe nada: `plan()` devuelve `0` y `found()` queda a `false`.
  `expansions()` informa del trabajo real y sirve para dimensionar el presupuesto.

### 3.4 Escenarios verificados

HOST-107 ejercita el planner con tres dominios clásicos (y los reproduce paso a paso):

| Escenario | Modelo | Plan | Coste | Nodos |
|---|---|---|---:|---:|
| Torres de Hanoi (3 discos) | un hecho «disco d en poste p» por disco/poste; 18 acciones generadas en `constexpr`; `forbid` de los discos menores | 7 | 7 | 17 |
| Receta de un pastel | 8 hechos (ingredientes, mezcla, horno, horneado, decorado); coste por acción | 8 | 20 | 35 |
| Misión de un soldado | obstáculo (alambre) + utensilio (alicates) + llave/puerta + máquina (generador/puerta eléctrica) + arma/munición | 13 | 26 | 81 |

Además, el test cubre los casos límite (objetivo ya cumplido, objetivo sin solución y
`forbid`) y declara **dos dominios de distinta clave** en la misma unidad de traducción: el de
32 hechos (`Goap<>`, el de los escenarios) y uno de 64 (`Goap<64>`) que usa el hecho `63`, el
último válido, y ejercita la clave de dos palabras (`StateKey64`). El contenedor `Ai::Domain`
se prueba con un problema mínimo (3 acciones encadenadas, coste 3).

## 4. Decisión por tick (`eng/ai/decision/`)

La decisión se apoya en dos motores genéricos de `eng::util` (no se duplican):
`StateMachine` (transiciones) y `Event` (difusión). Encima:

| Cabecera | Qué aporta |
|---|---|
| `agent_fsm.hpp` | `AgentFsm<State,Event,MaxStates>`: FSM con **efectos de entrada/salida** de estado, conteniendo `util::StateMachine`. |
| `utility.hpp` | `Utility` (media ponderada de consideraciones normalizadas en `[0,1000]`) y `UtilitySelector<MaxOptions>` (mejor opción, empate → índice menor). Sin `float` ni libcalls (`muls.w`/`divs.w`). |
| `behavior_tree.hpp` | `BehaviorTree<MaxNodes>` sin heap: secuencia y selector sobre hojas `FunctionRef<BtStatus()>`; tick síncrono y acotado. |
| `blackboard.hpp` | `Blackboard<Key,Value,MaxKeys>`: memoria compartida por claves densas, `find` `O(1)`. No emite; para difundir se usa `util::Event`. |

Coste medido (sondas m68k): `Blackboard::find` 3 instrucciones; `Utility` 40 instrucciones con
2 `muls.w`; `BehaviorTree::tick` (árbol de 3 nodos) 271 instrucciones (recursivo, sin
libcalls). Verificación: HOST-110 (FSM), HOST-111 (blackboard), HOST-112 (utility), HOST-113
(árbol).

## 5. Inventario

| Cabecera | Tipos / funciones | Estado |
|---|---|---|
| `planning/goap.hpp` | `Goap<MaxFacts>` (dominio: `State`/`state`/`Action`/`Builder`/`Goal`/`Planner`), `Fact`, `applicable`, `apply`, `satisfies`, `goal_distance` | Implementado, HOST-107 |
| `decision/agent_fsm.hpp` | `AgentFsm<State,Event,MaxStates>`: FSM de agente con efectos de entrada/salida sobre `eng::util::StateMachine` | Implementado, HOST-110 |
| `decision/utility.hpp` | `Utility`/`UtilitySelector<MaxOptions>`: utilidad ponderada; entero y determinista | Implementado, HOST-112 |
| `decision/behavior_tree.hpp` | `BehaviorTree<MaxNodes>`, `BtStatus`, `BtTask`: secuencia/selector sin heap | Implementado, HOST-113 |
| `decision/blackboard.hpp` | `Blackboard<Key,Value,MaxKeys>`: memoria compartida `O(1)` | Implementado, HOST-111 |
| `navigation/…` | navmesh lite (Recast/Detour), flow field | Planificado (ROADMAP_GAME_AI) |
| `steering/…` | seek/flee/arrive, flocking, evasión | Planificado (ROADMAP_GAME_AI) |
| `perception/…` | influence maps, sensores | Planificado (ROADMAP_GAME_AI) |
| `design/…` | director de dificultad, recompensas | Planificado (ROADMAP_GAME_AI) |

> Estado de verificación: los módulos de `eng::ai` están **verificados por test host**
> (GOAP: HOST-107; decisión: HOST-110…113). Al no tener todavía consumidor en una demo,
> están **NO VERIFICADOS por demo** (ver [docs/testing/README.md](../../testing/README.md));
> pueden cambiar sin aviso. La primera demo/juego con un personaje con objetivos los
> verificará en el 68000.

## 6. Cómo añadir una técnica

1. Comprobar que no existe ya en `eng/core/`, `eng/core/util/` ni `eng/ai/` (§1.6 de
   `AGENTS.md`); decidir si se reutiliza una primitiva existente.
2. Cabecera en `engine/include/eng/ai/<familia>/`, en `namespace eng::ai`, con comentario
   didáctico: intención, coste, límites por plataforma y ejemplo de uso.
3. Test host en `tests/host/` con su `README.md` y entrada en el catálogo
   ([tests/host/README.md](../../../tests/host/README.md)).
4. Si la pieza entra en un bucle por frame, añadir una sonda a
   `tools/analyze/codegen-report.mjs` (sin libcalls de libgcc ni instrucciones 68020).
   Las de planificación no lo necesitan: se ejecutan en `init`/fondo y el coste se acota
   con presupuesto.
5. Actualizar este documento (inventario y estado) y el roadmap, en la misma pasada.
6. Verificación por demo cuando exista consumidor; entonces deja de ser «NO VERIFICADA».

Plan de crecimiento y catálogo completo de técnicas:
[ROADMAP_GAME_AI.md](../../guides/roadmap/ROADMAP_GAME_AI.md).
