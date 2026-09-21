# Roadmap de IA de juego y técnicas de diseño (`eng::ai`)

Plan de crecimiento de `engine/include/eng/ai/` y **catálogo de técnicas** (de IA clásica
y de diseño de videojuego) que el engine quiere incorporar. Describe qué falta, en qué
orden y cómo se verifica; el estado vigente de lo entregado está en
[GAME_AI_LIBRARY.md](../../engine/architecture/GAME_AI_LIBRARY.md) (fuente de referencia,
no se duplica aquí).

## 1. Objetivo y criterio

Dotar al engine de las técnicas de IA de juego con **consumidor real** y encaje en el A500
(sin `malloc`, coste visible, determinismo): planificación, decisión, navegación, movimiento
y percepción. Las **técnicas de diseño** (pacing, dificultad, recompensas, niveles) se
documentan como patrones y solo se convierten en código cuando un juego de `games/` las
necesita; entonces se implementan en `eng::ai::design` con su test.

Cada pieza respeta las reglas transversales de §3 y se cierra con test host, cross-compile
y (si tiene consumidor natural) una demo/juego exitoso.

## 2. Estado de partida

- **Entregado**: `planning/goap.hpp` (GOAP con A*, hechos booleanos, acciones con coste;
  HOST-107) y la familia `decision/` (`agent_fsm`, `utility`, `behavior_tree`, `blackboard`;
  HOST-110…113), con la taxonomía e inventario en
  [GAME_AI_LIBRARY.md](../../engine/architecture/GAME_AI_LIBRARY.md).
- **Primitivas ya disponibles**: `eng::util::pathfinding` (BFS/A* en rejilla),
  `eng::util::broadphase` (`SpatialHash`), `eng::util::grid` (tile/iso/hex),
  `eng::util::random`, y los contenedores/heap/hashmap que usará la IA.
- **En `eng::util`** (no duplicar): `state_machine.hpp` (HOST-108) y `event.hpp` (HOST-109)
  **entregados**; `variant.hpp` sigue planificado (R4 de
  [ROADMAP_UTIL_LIBRARY.md](ROADMAP_UTIL_LIBRARY.md)); `decision` se apoya en ellos.
- **Consumidor**: `eng::sim` ([SIM_ECOSYSTEM.md](../../engine/architecture/SIM_ECOSYSTEM.md))
  usa la utilidad, la percepción, la navegación y el GOAP de `eng::ai` para el modelo de
  ecosistema (necesidades, personalidad, mente, conocimiento, jerarquía, genética, sociedad
  y LOD; HOST-152…155; `sim/planner.hpp` envuelve `Goap`). No es una técnica nueva de
  `eng::ai` sino una capa de entidad viva construida sobre la librería.

## 3. Reglas transversales (criterios de aceptación)

- **Sin heap**: estado inline o `scratch` del llamador; crecimiento solo en `init`.
- **Determinismo**: sin `float` en caminos reproducibles; preferir entero/fixed. Los
  desempates de búsqueda deben ser estables (p. ej. por orden de creación del nodo).
- **Coste visible**: presupuesto explícito (`MaxNodes`, tamaño del scratch) y cabecera con
  límites por plataforma; nada de trabajo no acotado por frame.
- **Fuera del camino caliente**: planificación y navegación de grafo se ejecutan en `init`
  o en `eng::task`; el seguimiento del plan sí puede ser por frame.
- **Verificación**: `tests/host/NNN` + `README.md`; sonda de codegen si entra en bucle por
  frame; demo/juego cuando exista consumidor; actualizar `GAME_AI_LIBRARY.md` y este
  roadmap en la misma pasada.
- **No duplicar**: reutilizar `eng::util`/`eng::math` antes de crear; una técnica que ya
  cubra una primitiva genérica extiende la primitiva, no la copia.

## 4. Fases

### G1 — Planificación de acciones

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| G1.1 | `planning/goap.hpp` | Hechos booleanos, acciones (pre/efectos/coste), A* con heurística de objetivos pendientes; presupuesto `MaxNodes` | **HOST-107** (Hanoi, pastel, soldado) |
| G1.2 | `planning/htn.hpp` (opcional) | Redes de tareas jerárquicas: descomponer un objetivo en subtareas con métodos alternativos | HOST propio; demo si hay consumidor |
| G1.3 | `planning/goap.hpp` (caché) | Cachear planes por `(estado, objetivo)` para no replanificar lo mismo cada vez | **Entregado**: `plan_cached` en HOST-107 |
| G1.4 | `planning/numeric_goap.hpp` | GOAP con **variables numéricas cuantizadas** (enteros y decimales por escala), saturación, `plan_cached` y reutilización de sufijo (`plan_reusing`) | **Entregado**: HOST-185 |
| G1.5 | `planning/numeric_goap.hpp` (heurística) | **Heurística de grafo relajado** (`h_max`) + cota numérica, con memo de `h` entre llamadas (`plan_relaxed`, `heuristic_hits`) | **Entregado**: HOST-186 |

**Estado: G1.1 completa.** GOAP verificado por HOST-107. Candidatos: HTN (G1.2) y caché de
planes (G1.3), ambos solo con consumidor.

### G2 — Decisión por tick

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| G2.1 | `decision/agent_fsm.hpp` | FSM/HSM reutilizando la máquina de estados genérica de `eng::util`; la capa de IA solo aporta estados/eventos y efectos (no una segunda FSM) | **Entregado**: `util::StateMachine` (HOST-108) + `ai::AgentFsm` (HOST-110) |
| G2.2 | `decision/utility.hpp` | Utilidad por puntuación (media ponderada de consideraciones), entera y determinista (`muls.w`/`divs.w`) | **Entregado**: HOST-112 |
| G2.3 | `decision/behavior_tree.hpp` | Secuencia/selector sobre nodos sin heap; tick síncrono y acotado | **Entregado**: HOST-113 |
| G2.4 | `decision/blackboard.hpp` + eventos | Datos compartidos entre sistemas; difusión con `event.hpp` (entregado, HOST-109) | **Entregado**: HOST-111 |
| G2.5 | `decision/` HFSM | FSM **jerárquica** (estados compuestos) sobre `AgentFsm`, si aparecen estados anidados | HOST propio; con consumidor |

**Estado: G2 completa.** Reutiliza `state_machine` (R4.4) y `event` (R4.3) de `eng::util`, y
añade `AgentFsm`, `Utility`, `BehaviorTree` y `Blackboard` (HOST-110…113). Siguiente: G3
(navegación) o G4 (movimiento/percepción).

### G3 — Navegación

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| G3.1 | `navigation/flow_field.hpp` | Campo de flujo sobre rejilla (Dijkstra multi-fuente con coste de terreno) para muchos agentes | **Entregado**: HOST-114 |
| G3.2 | `navigation/waypoints.hpp` | Grafo de waypoints + A* sobre el grafo | **Entregado**: HOST-116 |
| G3.3 | `navigation/navmesh_lite.hpp` | Versión **lite** de Recast/Detour: polígonos convexos + portales, punto en polígono y A* de adyacencia | **Entregado**: HOST-118 (referencia <https://github.com/recastnavigation/recastnavigation>) |

G3 se apoya en `eng::util::pathfinding`/`grid`; el navmesh lite se acota a mallas pequeñas
de A500 (memoria y coste de consulta visibles). **Estado: G3 completa** (flow field, waypoints y navmesh
lite; HOST-114/116/118).

### G4 — Movimiento y percepción

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| G4.1 | `steering/steering.hpp` | seek/flee/arrive y separación/cohesión/alineación (flocking), genérico sobre el escalar | **Entregado**: HOST-115 |
| G4.2 | `perception/influence_map.hpp` | Mapa de influencia (amenaza/control) sobre rejilla con decay | **Entregado**: HOST-117 |
| G4.3 | `perception/agent_memory.hpp` | Memoria del agente (última posición conocida, tiempo desde el avistamiento) | **Entregado**: HOST-117 |
| G4.4 | `steering/steering.hpp` | *Pursue*/*evade* (con velocidad del objetivo), *wander* y evasión de obstáculos | **Entregado**: HOST-115 |
| G4.5 | `steering/formation.hpp` | Formación / asignación de huecos respecto a un líder o centro | HOST propio; con consumidor |

G4 no depende de G1–G3; puede adelantarse si un juego necesita movimiento. **Estado: G4.1–G4.3 entregados** (steering, influence
map y memoria; HOST-115/117). Candidatos G4.4 (pursue/evade/wander/evasión) y G4.5 (formación).

### G5 — Técnicas de diseño con consumidor

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| G5.1 | `design/difficulty_director.hpp` | Ajuste dinámico de dificultad (pacing, «rubber banding») con parámetros de diseño explícitos | HOST + juego en `games/` |
| G5.2 | `design/reward_scheduler.hpp` | Refuerzo por intervalos/ratio variable determinista para drops y feedback | HOST + juego |
| G5.3 | `design/` (otras) | Solo cuando un juego de `games/` lo necesite | HOST + juego |

G5 exige un consumidor real (juego en `games/`); hasta entonces estas técnicas viven como
patrones descritos en §5.

### G6 — Crowd y mejoras de navegación

Recoge la revisión de las mejoras propuestas para la navegación lite (`navmesh_lite`/`flow_field`/
`waypoints`) y el crowd. Lo que **ya existía** no se duplica: el campo de amenaza/interés es
`perception/influence_map.hpp`, la separación/evasión/flocking es `steering.hpp`, el eje
Abstract/Realized es el LOD de `eng::sim` (`lod.hpp`), y no se añade una interfaz `INavigator`
virtual (el engine usa funciones libres y buffers externos).

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| G6.1 | `steering/crowd.hpp` | `Crowd<S, Broadphase>` **genérico sobre el escalar** (separación + evasión + integración); fase amplia de vecinos como política (`SpatialHashBroadphase` evita el `O(N²)`, `BruteForceBroadphase` vale para cualquier escalar) | **Entregado**: HOST-249 (con `s32` y `float`) |
| G6.2 | `navmesh_lite.hpp` | Cache/atajo de `locate`: recordar el polígono actual y probar vecinos antes del bucle lineal | HOST propio |
| G6.3 | `navmesh_lite.hpp`/`waypoints.hpp` | **Coste por portal** (terreno/peligro) y `MovementProfile` (can_climb/can_swim/max_slope) que filtra portales/costes | HOST propio |
| G6.4 | navegación | **Reutilización de paths**: recalcular solo si el objetivo se movió o el agente se desvió (patrón de la caché de planes del GOAP) | HOST propio |
| G6.5 | `navmesh_lite.hpp` | **Budget explícito** de nodos expandidos por agente y frame | HOST propio |
| G6.6 | `steering/crowd.hpp` | Crowd **fixed-point pura** (sin `/` ni `isqrt` por vecino) y sonda de codegen | HOST + `codegen-report.mjs` |

G6.1 está entregado y verificado por test host; G6.2–G6.6 quedan **pendientes** (se abordan cuando
haya un consumidor de movimiento con muchos agentes). El crowd es **genérico sobre el escalar** y
recibe la **fase amplia de vecinos como política** (regla de genericidad, `AGENTS.md` §1.10): la
variante `SpatialHashBroadphase` reutiliza la rejilla de colisiones ya existente
(`eng::util::SpatialHash`, `broadphase.hpp`) para evitar el `O(N²)`; `BruteForceBroadphase` cubre
cualquier escalar. Así el algoritmo no queda atado a `s16` ni a una rejilla concreta.

## 5. Catálogo de técnicas a incorporar

### 5.1 IA clásica

| Técnica | Familia | Destino previsto | Referencia | Estado |
|---|---|---|---|---|
| GOAP (Goal-Oriented Action Planning) | planificación | `ai/planning/goap.hpp` | A. Alex (basado en Orkin/F.E.A.R.) | **Implementado** (HOST-107) |
| HTN (Hierarchical Task Network) | planificación | `ai/planning/htn.hpp` | Erol, Hendler, Nau | Pendiente |
| FSM / HSM | decisión | `util/state_machine.hpp` (motor) + `ai/decision/agent_fsm.hpp` (efectos) | — | **Entregado** (HOST-108 y HOST-110) |
| Utility AI | decisión | `ai/decision/utility.hpp` | D. Mark (Infinite Axis Utility System) | **Entregado** (HOST-112) |
| Behavior trees | decisión | `ai/decision/behavior_tree.hpp` | — | **Entregado** (HOST-113) |
| Blackboard / eventos | decisión | `ai/decision/blackboard.hpp` + `util/event.hpp` | — | **Entregado** (HOST-111 / HOST-109) |
| Flow field | navegación | `ai/navigation/flow_field.hpp` | — | **Entregado** (HOST-114) |
| Grafo de waypoints + A* | navegación | `ai/navigation/waypoints.hpp` | — | **Entregado** (HOST-116) |
| Navmesh lite (Recast/Detour) | navegación | `ai/navigation/navmesh_lite.hpp` | <https://github.com/recastnavigation/recastnavigation> | **Entregado** (HOST-118) |
| Steering behaviors / flocking | movimiento | `ai/steering/steering.hpp` | C. Reynolds (1987) | **Entregado** (HOST-115) |
| Influence maps | percepción | `ai/perception/influence_map.hpp` | (técnica de RTS) | **Entregado** (HOST-117) |
| Memoria del agente / creencias | percepción | `ai/perception/agent_memory.hpp` | — | **Entregado** (HOST-117) |
| Caché de planes GOAP | planificación | `ai/planning/goap.hpp` (`plan_cached`) | — | **Entregado** (HOST-107) |
| GOAP numérico cuantizado (enteros y decimales) | planificación | `ai/planning/numeric_goap.hpp` | Alex/Orkin (extensión numérica) | **Entregado** (HOST-185) |
| Heurística relajada (h_max) + memo de h | planificación | `ai/planning/numeric_goap.hpp` (`plan_relaxed`) | relajación por borrado (Hoffmann/Nebel) | **Entregado** (HOST-186) |
| HFSM (FSM jerárquica) | decisión | `ai/decision/` | — | Candidato (estados anidados) |
| Pursuit/evade/wander y evasión de obstáculos | movimiento | `ai/steering/steering.hpp` | C. Reynolds | **Entregado** (HOST-115) |
| Crowd (separación + evasión local) | movimiento | `ai/steering/crowd.hpp` | — | **Entregado** (HOST-249) |
| Cache de `locate` / coste por portal + `MovementProfile` | navegación | `ai/navigation/navmesh_lite.hpp` | Recast/Detour | Pendiente (G6.2–G6.3) |
| Reutilización de paths + budget de expansión | navegación | navegación | — | Pendiente (G6.4–G6.5) |
| Formación / asignación de huecos | movimiento | `ai/steering/` | — | Candidato opcional |
| Heurística parametrizable (grilla) | navegación | `util/pathfinding.hpp` | — | Candidato menor |
| SAT 2D (polígonos convexos) | colisión (`util`) | `util/collision.hpp` | — | Candidato |
| Autómata celular | procedural | `util/` o demo | — | Candidato menor |
| Filtmation | presentación/animación | por definir | *Heads over Heels* / *Batman 3D* | Pendiente de ficha técnica |

> **Filtmation**: la técnica aún no está documentada en el repositorio. La ficha debe
> aclarar el nombre, el efecto observable y su implementación (relación con sprites/Copper
> u overlays) antes de proponer una cabecera; hasta entonces no se implementa.

### 5.2 Diseño de videojuego (patrones; código solo con consumidor)

| Técnica | Para qué | Posible pieza de engine |
|---|---|---|
| MDA (mecánicas–dinámicas–estética) | Lenguaje común de diseño | Documento de metodología |
| Game feel / «juice» | Feedback inmediato (sacudida, hit-stop, partículas) | `design/` (efectos como parámetros de diseño) |
| Curvas de dificultad y DDA | Pacing y ajuste dinámico | `design/difficulty_director.hpp` |
| Recompensas (intervalo/ratio variable) | Motivación y economía | `design/reward_scheduler.hpp` |
| Flow / intensidad | Alternar tensión y calma | Documento + hooks en el juego |
| Faucets y sinks (economía) | Control de recursos | Documento + reglas del juego |
| Diseño de niveles (gating, breadcrumbing, landmarks) | Guiar al jugador | Documento + editor/pipeline de mapas |
| Enseñar sin texto (teach-then-test) | Tutorialización | Documento de metodología |

Estos patrones se documentan primero (sin duplicar fuentes) y se convierten en código solo
cuando un juego de `games/` los use. Referencias formales (MDA, Swink, Reynolds, Mark…)
se citarán en la ficha de cada uno al incorporarlos.

### 5.3 Evaluación de la lista externa de núcleos clásicos

Lista de núcleos clásicos propuesta externamente para un engine «estilo Amiga 500». Casi toda
ella está ya cubierta; se anotan las piezas que faltan y las que se descartan de forma
explícita.

| Bloque de la lista | Estado en el engine | Decisión |
|---|---|---|
| A* (heurísticas intercambiables, early-exit, reutilización) | `util::pathfinding` (A*/BFS, Manhattan, early-exit, scratch del llamador) + `ai::navigation.*` | Cubierto; **candidata** la heurística parametrizable (Euclídea/Octile) y reutilizar scratch entre consultas |
| Dijkstra | A* con `h=0`; `flow_field` es Dijkstra multi-fuente | Cubierto; no se añade pieza aparte |
| Jump Point Search / Theta* | — | **Descartado**: mapas pequeños (8×8..32×32); el coste y la complejidad no compensan |
| Flood-fill / distance field | `ai/navigation/flow_field.hpp` (`integration` + `direction`) | Cubierto |
| Navmesh simplificado + funnel | `ai/navigation/navmesh_lite.hpp` (`find_smooth_path`) | Cubierto |
| GOAP/STRIPS + A* + **caché de planes** | `ai/planning/goap.hpp`; la caché no | **Candidata** la caché por (estado, objetivo), con consumidor |
| FSM e **HFSM** | `util::state_machine` + `ai::AgentFsm` (plana); HFSM no | **Candidata** la HFSM si aparecen estados anidados |
| Utility AI | `ai/decision/utility.hpp` | Cubierto |
| Behavior trees ultra-simplificados | `ai/decision/behavior_tree.hpp` (secuencia/selector/hoja; condición y acción son hojas) | Cubierto |
| Steering (pursue/evade/wander/obstacle avoidance) | `ai/steering/steering.hpp` (seek/flee/arrive/flocking); faltan pursue, evade, wander y evasión | **Candidato** (G4.4) |
| Separación/alineación O(n) con spatial hash | `util/broadphase.hpp` + steering | Cubierto |
| Formación / asignación de huecos | — | **Candidato opcional** (posiciones relativas a líder/centro), con consumidor |
| Influence maps / potential fields | `ai/perception/influence_map.hpp` | Cubierto |
| Spatial hash / grid | `util/broadphase.hpp`, `util/grid.hpp` | Cubierto |
| Quadtree ligero | — | **Descartado**: `SpatialHash` cubre y es más simple en 2D |
| AABB/círculo + SAT/GJK | `util/collision.hpp` (AABB/segmento/triángulo/círculo); SAT no | **Candidato** SAT 2D; GJK descartado (sobra para 2D) |
| Sweep and prune | — | **Descartado**: `SpatialHash` cubre |
| Bitmask / tile collision | mapas y `field` tile-based | Cubierto |
| Heap, pool/free-list, ring buffer | `priority_queue`, `pool`/`intrusive_list`, `ring_buffer` | Cubierto |
| Fixed-point helpers | `Fixed`/`q12`/`q24` + `fixed_math` | Cubierto |
| RNG determinista (xorshift/PCG) | `core/random.hpp` (`Xoroshiro64pp`) | Cubierto; PCG no se añade |
| Sorts estables/cache-friendly | `quick_sort`/`stable_sort`/`radix_sort_u16` | Cubierto |
| Lerp/smoothstep/bezier/easing | `interp`/`scalar_ops` | Cubierto |
| Noise + random walks + CA | `core/noise.hpp` (value/perlin/worley/turbulence); CA ad-hoc | Cubierto; **candidata menor** una utilidad de autómata celular con consumidor |
| Diseño (header-only, allocators, SoA, constexpr, full/ultra-light, determinismo) | Ya es la política del repo (`CODING_STYLE`, `TEMPLATE_LIBRARY`) | Sin cambios |

**Conclusión**: la lista está cubierta en lo esencial y su orden de prioridad (spatial hash +
A* + steering; FSM + utility; GOAP; navmesh/funnel + influence; pools + fixed-point) coincide
con G1–G4, ya entregados. Candidatos vivos: caché de planes GOAP, HFSM, *pursue*/*evade*,
*wander* y evasión de obstáculos, formación, heurística parametrizable y SAT 2D. Se descartan
JPS/Theta*, quadtree, sweep-and-prune, GJK, RVO/ORCA y PCG por coste/complejidad frente a
alternativas ya presentes.

## 6. Dependencias entre fases

```
   eng::util (state_machine entregado; event/variant R4)
        │
        ▼
   G2 decisión ──► G1 planificación (GOAP ya hecho; HTN opcional)
        │
        ▼
   G3 navegación (reutiliza util::pathfinding/grid)
        │
        ▼
   G4 movimiento/percepción          G5 diseño (exige juego en games/)
```

G1 está hecho y es independiente. G2 reutiliza el `state_machine` ya entregado (y espera `event` para el blackboard). G3/G4 no dependen de G2. G5 es el
más tardío: no hay código de diseño sin un juego que lo consuma.

## 7. Riesgos y decisiones abiertas

- **Presupuesto de búsqueda**: GOAP y navmesh compiten por RAM/tiempo; fijar presupuestos
  por escenario (`MaxNodes`, tamaño de malla) y ejecutar en `init`/fondo.
- **Representación del mundo del juego**: decidir si los hechos de GOAP se derivan de los
  componentes de la escena o se mantienen como capa propia; evitar sincronizaciones
  manuales frágiles.
- **Navmesh lite**: acotar a mallas pequeñas; decidir formato de malla y si se cuece en
  host (`tools/`) o en `init`.
- **Heurística de GOAP**: la actual es admisible solo si cada acción satisface como mucho
  un hecho del objetivo; decidir si hace falta una heurística relajada (h_add) si aparecen
  planes con acciones multi-objetivo.
- **Filtmation**: sin ficha técnica no se planifica; documentarla antes de prometer nada.

## 8. Cómo se cierra cada paso

1. Cabecera en `engine/include/eng/ai/<familia>/` con comentario didáctico.
2. `tests/host/NNN` + `README.md` y registro en el catálogo de tests.
3. Sonda de codegen si entra en bucle por frame.
4. Actualizar `GAME_AI_LIBRARY.md` (inventario/estado) y este roadmap (fase hecha).
5. Commit atómico por paso, con la referencia a la fuente y a los tests.
