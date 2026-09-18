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

- **Entregado**: `planning/goap.hpp` (GOAP con A*, hechos booleanos, acciones con coste),
  verificado por HOST-107 (Hanoi, pastel, soldado) y con la taxonomía documentada en
  [GAME_AI_LIBRARY.md](../../engine/architecture/GAME_AI_LIBRARY.md).
- **Primitivas ya disponibles**: `eng::util::pathfinding` (BFS/A* en rejilla),
  `eng::util::broadphase` (`SpatialHash`), `eng::util::grid` (tile/iso/hex),
  `eng::util::random`, y los contenedores/heap/hashmap que usará la IA.
- **En `eng::util`** (no duplicar): `state_machine.hpp` (HOST-108) y `event.hpp` (HOST-109)
  **entregados**; `variant.hpp` sigue planificado (R4 de
  [ROADMAP_UTIL_LIBRARY.md](ROADMAP_UTIL_LIBRARY.md)); `decision` se apoya en ellos.

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

**Estado: G1.1 completa.** GOAP verificado por HOST-107. HTN solo si un juego lo pide.

### G2 — Decisión por tick

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| G2.1 | `decision/agent_fsm.hpp` | FSM/HSM reutilizando la máquina de estados genérica de `eng::util`; la capa de IA solo aporta estados/eventos y efectos (no una segunda FSM) | **Entregado**: `util::StateMachine` (HOST-108) + `ai::AgentFsm` (HOST-110) |
| G2.2 | `decision/utility.hpp` | Utilidad por puntuación (suma ponderada de consideraciones), con normalización/clamp enteros | HOST propio |
| G2.3 | `decision/behavior_tree.hpp` | Selector/secuencia/decoradores sobre nodos sin heap; tick por frame acotado | HOST propio |
| G2.4 | `decision/blackboard.hpp` + eventos | Datos compartidos entre sistemas; difusión con `event.hpp` (entregado, HOST-109) | HOST propio |

Dependencia: G2 reutiliza `state_machine` (entregado, R4.4) y `event` (entregado, R4.3) de
`eng::util`. Queda utility AI y behavior tree sobre esa base.

### G3 — Navegación

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| G3.1 | `navigation/flow_field.hpp` | Campo de flujo sobre rejilla (Dijkstra/BFS desde el destino) para muchos agentes | HOST propio; reutiliza `pathfinding`/`grid` |
| G3.2 | `navigation/waypoints.hpp` | Grafo de waypoints + A* sobre el grafo | HOST propio |
| G3.3 | `navigation/navmesh_lite.hpp` | Versión **lite** de Recast/Detour: malla de navegación, punto más cercano y consulta de camino sobre polígonos | HOST propio + referencia <https://github.com/recastnavigation/recastnavigation> |

G3 se apoya en `eng::util::pathfinding`/`grid`; el navmesh lite se acota a mallas pequeñas
de A500 (memoria y coste de consulta visibles).

### G4 — Movimiento y percepción

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| G4.1 | `steering/` | seek/flee/arrive/wander, separación/cohesión/alineación (flocking), evasión; enteros/fixed | HOST propio |
| G4.2 | `perception/influence_map.hpp` | Mapa de influencia (amenaza/control) sobre rejilla, decay y difusión acotada | HOST propio |
| G4.3 | `perception/` | Memoria del agente (última posición conocida, tiempo desde el avistamiento) | HOST propio |

G4 no depende de G1–G3; puede adelantarse si un juego necesita movimiento.

### G5 — Técnicas de diseño con consumidor

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| G5.1 | `design/difficulty_director.hpp` | Ajuste dinámico de dificultad (pacing, «rubber banding») con parámetros de diseño explícitos | HOST + juego en `games/` |
| G5.2 | `design/reward_scheduler.hpp` | Refuerzo por intervalos/ratio variable determinista para drops y feedback | HOST + juego |
| G5.3 | `design/` (otras) | Solo cuando un juego de `games/` lo necesite | HOST + juego |

G5 exige un consumidor real (juego en `games/`); hasta entonces estas técnicas viven como
patrones descritos en §5.

## 5. Catálogo de técnicas a incorporar

### 5.1 IA clásica

| Técnica | Familia | Destino previsto | Referencia | Estado |
|---|---|---|---|---|
| GOAP (Goal-Oriented Action Planning) | planificación | `ai/planning/goap.hpp` | A. Alex (basado en Orkin/F.E.A.R.) | **Implementado** (HOST-107) |
| HTN (Hierarchical Task Network) | planificación | `ai/planning/htn.hpp` | Erol, Hendler, Nau | Pendiente |
| FSM / HSM | decisión | `util/state_machine.hpp` (motor) + `ai/decision/agent_fsm.hpp` (efectos) | — | **Entregado** (HOST-108 y HOST-110) |
| Utility AI | decisión | `ai/decision/utility.hpp` | D. Mark (Infinite Axis Utility System) | Pendiente |
| Behavior trees | decisión | `ai/decision/behavior_tree.hpp` | — | Pendiente |
| Blackboard / eventos | decisión | `ai/decision/blackboard.hpp` | — | Pendiente |
| Flow field | navegación | `ai/navigation/flow_field.hpp` | — | Pendiente |
| Grafo de waypoints + A* | navegación | `ai/navigation/waypoints.hpp` | — | Pendiente |
| Navmesh lite (Recast/Detour) | navegación | `ai/navigation/navmesh_lite.hpp` | <https://github.com/recastnavigation/recastnavigation> | Pendiente |
| Steering behaviors / flocking | movimiento | `ai/steering/` | C. Reynolds (1987) | Pendiente |
| Influence maps | percepción | `ai/perception/influence_map.hpp` | (técnica de RTS) | Pendiente |
| Memoria del agente / creencias | percepción | `ai/perception/` | — | Pendiente |
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
