# Tests HOST — ai

Categoría `ai` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-107 | [goap](107_goap/README.md) | `ai/planning/goap.hpp`: planificador GOAP (hechos booleanos, acciones con coste, A*). Planes óptimos para Torres de Hanoi (7), receta de un pastel (coste 20) y misión de un soldado (coste 26), con `forbid` y casos límite. |
| HOST-108 | [state_machine](108_state_machine/README.md) | `util/state_machine.hpp`: FSM de tabla `constexpr` externa (`StateMachine<State,Event>`, `Transition`). Semáforo y FSM de IA de un guardia; evento sin transición, `reset` y orden de tabla. |
| HOST-110 | [agent_fsm](110_agent_fsm/README.md) | `ai/decision/agent_fsm.hpp`: `AgentFsm` envuelve `util::StateMachine` y añade efectos de entrada/salida de estado; evento sin transición y reemplazo de efecto. |
| HOST-111 | [blackboard](111_blackboard/README.md) | `ai/decision/blackboard.hpp`: `Blackboard<Key,Value,MaxKeys>` (memoria compartida de la IA) con claves densas; `find` O(1), sobrescritura y valores struct. |
| HOST-112 | [utility](112_utility/README.md) | `ai/decision/utility.hpp`: `Utility` (media ponderada en [0,1000], `muls.w`/`divs.w`) y `UtilitySelector` (mejor opción, empate -> índice menor). Decisión de un guardia. |
| HOST-113 | [behavior_tree](113_behavior_tree/README.md) | `ai/decision/behavior_tree.hpp`: `BehaviorTree<MaxNodes>` sin heap (secuencia/selector, cortocircuito, `no_node` al llenarse). Guardia dispara/recarga. |
| HOST-114 | [flow_field](114_flow_field/README.md) | `ai/navigation/flow_field.hpp`: campo de flujo por Dijkstra multi-fuente (`compute_flow_field<W,H>`, `flow_next<W>`); coste uniforme, muro, región inalcanzable. |
| HOST-115 | [steering](115_steering/README.md) | `ai/steering/steering.hpp`: `seek`/`flee`/`arrive`, flocking (`separation`/`cohesion`/`alignment`/`flock`) y `pursue`/`evade`/`wander`/`avoid_circles`, genérico sobre `double` y `q12`. |
| HOST-116 | [waypoints](116_waypoints/README.md) | `ai/navigation/waypoints.hpp`: `WaypointGraph<MaxNodes,MaxEdges>` + A* sobre el grafo (heurística Manhattan, scratch del llamador). Ruta óptima, inalcanzable y capacidad. |
| HOST-117 | [perception](117_perception/README.md) | `ai/perception/influence_map.hpp` (`InfluenceMap<W,H>`: deposit/decay/strongest) y `ai/perception/agent_memory.hpp` (`AgentMemory`: see/tick/fresh/stale/forget). |
| HOST-118 | [navmesh](118_navmesh/README.md) | `ai/navigation/navmesh_lite.hpp`: `NavMesh` de polígonos convexos con portales; localizar punto, A* por adyacencia, puntos medios y string-pulling (funnel). |
| HOST-185 | [goap_numeric](185_goap_numeric/README.md) | `ai/planning/numeric_goap.hpp`: GOAP con variables numéricas cuantizadas (enteros y decimales), saturación y caché (memo de planes + sufijo). |
| HOST-186 | [goap_numeric_relaxed](186_goap_numeric_relaxed/README.md) | GOAP numérico con heurística relajada (h_max) y memo de heurística entre llamadas. |
| HOST-249 | [crowd](249_crowd/README.md) | Crowd genérico (`eng/ai/steering/crowd.hpp`): separación/evasión con fase amplia como política (`SpatialHash`, no `O(N²)`), probado con `s32` y `float`. |
