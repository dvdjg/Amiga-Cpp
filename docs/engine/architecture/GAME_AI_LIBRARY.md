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
- La máquina de estados genérica (`state_machine.hpp`) está planificada en
  [ROADMAP_UTIL_LIBRARY.md](../../guides/roadmap/ROADMAP_UTIL_LIBRARY.md) §R4 dentro de
  `eng::util`; `eng::ai::decision` se construirá **sobre** ella (no se duplica).

## 2. Organización por familias

Cada familia de técnicas tiene su subcarpeta y su espacio de nombres (`eng::ai`), de
modo que añadir una técnica nueva no reordena nada:

```
engine/include/eng/ai/
├── planning/     → PLANIFICACIÓN de acciones (GOAP; futuro HTN, planificador jerárquico)
├── decision/     → DECISIÓN por tick (FSM/HFSM, utility AI, behavior trees, selectores)
├── navigation/   → NAVEGACIÓN en el mapa (navmesh lite tipo Recast/Detour, flow fields)
├── steering/     → MOVIMIENTO continuo (seek/flee/arrive, flocking, evasión)
├── perception/   → MEMORIA y creencias (influence maps, blackboard, sensores, threat)
└── design/       → DISEÑO con consumidor (director de dificultad, recompensas, pacing)
```

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

| Tipo | Papel |
|---|---|
| `Fact` (`u16`) | índice de un hecho booleano; su significado lo fija el juego |
| `WorldState` | conjunto de hasta `max_facts` (32) hechos (`BitSet<32>`); `key()` lo empaqueta en `u32` |
| `make_state(hechos…)` | construye un estado a partir de sus hechos (constantes o de runtime) |
| `Action` | `pre_true`, `pre_false`, `eff_add`, `eff_del`, `cost`, `name` |
| `ActionBuilder` | constructor fluido (`named`/`cost`/`require`/`forbid`/`produce`/`consume`) |
| `Goal` | `want_true` (hechos exigidos a 1) y `want_false` (exigidos a 0) |
| `Planner<MaxNodes>` | A* hacia delante; `plan()`, `found()`, `plan_cost()`, `expansions()` |

El número de hechos **no se parametriza**: el estado se empaqueta en un `u32`, así que 32 es
el tope natural y no hay que repetir el tamaño en cada acción ni contenedor. El único
parámetro de plantilla de la API es el presupuesto de búsqueda del `Planner` (`MaxNodes`),
que tiene un valor por defecto.

Consulta de estado: `applicable(s, a)`, `apply(s, a)`, `satisfies(s, g)` y la
heurística `goal_distance(s, g)` (hechos del objetivo pendientes).

### 3.2 Uso

```cpp
enum : eng::u16 { kHarina, kHuevos, kMezcla, kHorneado };
constexpr eng::util::Array<eng::ai::Action, 3> acciones { {
    eng::ai::ActionBuilder{}.named("comprar").produce(kHarina, kHuevos).build(),
    eng::ai::ActionBuilder{}.named("batir").require(kHarina, kHuevos).produce(kMezcla).build(),
    eng::ai::ActionBuilder{}.named("hornear").require(kMezcla).produce(kHorneado).build(),
} };
eng::ai::Goal meta;
meta.want_true.facts.set(kHorneado);

eng::ai::Planner<64> planner;            // en Amiga: instancia estatica, no de pila
eng::u16 plan[8];
const eng::usize n = planner.plan(eng::ai::make_state(), meta, acciones.span(),
                                  eng::Span<eng::u16> {plan, 8u});
```

`plan()` escribe en `plan` los índices de acción (de primero a último) y devuelve su
número. Un plan vacío con `found() == true` significa que el estado ya cumplía el
objetivo; `found() == false` con `0` significa que no hay solución (o no cabe en
`out`/`MaxNodes`).

### 3.3 Coste y límites (A500)

- El planificador usa `HashMap`, `PriorityQueue` y los nodos **inline** en el objeto
  `Planner`. `MaxNodes` dimensiona los tres: `Planner<256>` ocupa ~9 KiB, así que se
  instancia en **memoria estática** (no en la pila del 68000) y se ejecuta en `init` o en
  una tarea de fondo (`eng::task`), nunca en el camino por frame.
- `max_facts = 32`: la clave del estado se empaqueta en una palabra de 32 bits (`WorldState`
  es un `BitSet<32>` fijo), de ahí la deduplicación `O(1)` sin hashes largos. Un índice de
  hecho fuera de `[0, 32)` dispara `illegal` (contrato de `BitSet`).
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

Además, el test cubre los casos límite: objetivo ya cumplido, objetivo sin solución y
`forbid`.

## 4. Inventario

| Cabecera | Tipos / funciones | Estado |
|---|---|---|
| `planning/goap.hpp` | `Fact`, `WorldState`, `make_state`, `Action`, `ActionBuilder`, `Goal`, `applicable`, `apply`, `satisfies`, `goal_distance`, `Planner` | Implementado, HOST-107 |
| `decision/…` | FSM/HFSM, utility AI, behavior trees | Planificado (ROADMAP_GAME_AI) |
| `navigation/…` | navmesh lite (Recast/Detour), flow field | Planificado (ROADMAP_GAME_AI) |
| `steering/…` | seek/flee/arrive, flocking, evasión | Planificado (ROADMAP_GAME_AI) |
| `perception/…` | influence maps, blackboard | Planificado (ROADMAP_GAME_AI) |
| `design/…` | director de dificultad, recompensas | Planificado (ROADMAP_GAME_AI) |

> Estado de verificación: `goap.hpp` está **verificado por test host** (HOST-107). Al no
> tener todavía consumidor en una demo, está **NO VERIFICADO por demo** (ver
> [docs/testing/README.md](../../testing/README.md)); puede cambiar sin aviso. La primera
> demo/juego que plantee un personaje con objetivos lo verificará en el 68000.

## 5. Cómo añadir una técnica

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
