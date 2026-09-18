# Roadmap de la librería de utilidades (`eng::util`)

Plan de crecimiento de `engine/include/eng/core/util/` y de las librerías genéricas de
`eng/`. Describe **qué falta, en qué orden y cómo se verifica**; el estado vigente de lo
ya entregado está en [TEMPLATE_LIBRARY.md](../../engine/architecture/TEMPLATE_LIBRARY.md)
(fuente de referencia, no se duplica aquí).

## 1. Objetivo y criterio

Ampliar el vocabulario genérico del engine con piezas de tipo Boost/STL que tengan
**consumidor real** y encajen en el A500 (sin `malloc`, coste visible, determinismo). No
se porta una librería entera: se añaden utilidades que se usan y se pueden verificar.

Cada pieza respeta las reglas transversales de §3 y se cierra con test host, cross-compile
y (si tiene consumidor natural) una demo exitosa.

## 2. Estado de partida

Ya entregado y verificado por test host: base (`type_traits`, `util`, `bit`,
`algorithm`, `array`, `bitset`), contenedores (`static_vector`, `small_vector`, `vector`,
`chunked_vector`, `ring_buffer`, `intrusive_list`, `pool`, `priority_queue`, `flat_map`,
`flat_set`, `hash_map`, `hash_set`, `dynamic_hash_map`, `direct_map`), utilidades de valor
(`optional`, `expected`, `string_view`, `static_string`, `scope_guard`, `function_ref`,
`enum_set`, `stack_queue`), ordenación (`quick_sort`, `stable_sort`, `nth_element`,
`partial_sort`, `radix_sort_u16`), `hash` y sondas de codegen. Extras de decisión,
partición, bits y texto: `state_machine` (R4.4), `event` (R4.3), `union_find` (DSU),
`sparse_set` (disperso-denso, ECS), `bitstream`/`dynamic_bitset` (R4.1), `string_interner`
(HOST-124), `graph` (HOST-126) y SAT 2D en `collision` (HOST-125). Verificados **por demo**:
`BitSet`, `StaticVector`, `RingBuffer`,
`FlatMap`, `DirectMap`, `IntrusiveSList`, `Pool`, `HashMap`.

Las matemáticas de escalares (`Fixed`, `MiniFloat16`, `linalg`, `interp`, `geometry`,
`spline`, `noise`) viven en `eng::math` y ya están cubiertas.

## 3. Reglas transversales (criterios de aceptación)

- **Sin heap**: capacidad fija inline o crecimiento vía `Allocator` (arena) solo en `init`.
- **División explícita**: lo que divide llama `require_division<S>()`; con `Fixed` usa
  `div_norm` (la API no expone `operator/`).
- **Determinismo**: sin `float` en caminos que deban ser reproducibles; preferir entero/fixed.
- **Coste visible**: límites por plataforma en la cabecera; sin asignaciones ocultas.
- **Verificación**: `tests/host/HOST-NNN` + `README.md`; sonda en
  `tools/analyze/codegen-report.mjs` si entra en bucle caliente; demo cuando exista
  consumidor; actualizar `TEMPLATE_LIBRARY.md` (inventario/tests/estado) en la misma pasada.
- **No duplicar** (§1.6 de `AGENTS.md`): reutilizar lo existente antes de crear.

## 4. Fases

### R1 — Juego base (matemáticas, color, colisión, texto)

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| R1.1 | `stats.hpp` | `mean`, `variance`/`stddev` (Welford una pasada), `median`/`percentile` (copia + `nth_element`), `histogram`, `ema`/`moving_average` (`RingBuffer`) | HOST-093 (double, `MiniFloat16`, `q12`); codegen de la división |
| R1.2 | `color.hpp` | `rgb444` pack/unpack, `lerp444`, `blend444`, `hsv_to_rgb444`, brillo/contraste | HOST-094; adopta el `lerp444` de la demo 086 |
| R1.3 | `collision.hpp` | `AABB` overlap, `circle_circle`, `segment_segment` (con normal), `point_in_triangle`, `ray_aabb`, barrido AABB | HOST-095; reutiliza `geometry.hpp`/`retro/lib2d` |
| R1.4 | `text.hpp` | `parse_u32/s32`, `to_chars` a `StaticString`, `trim`, `split_next`, `join` | HOST-096; pareja de `StringView`/`StaticString` |

Cierre de R1: los cuatro tests host verdes, cross-compile, doc actualizada. `stats` es la
respuesta directa a "matemáticas de estadística".

**Estado: R1 completa** (`stats.hpp` HOST-093, `color.hpp` HOST-094, `collision.hpp`
HOST-095, `text.hpp` HOST-096; sondas `c_stats_ops`/`c_color_lerp`/`c_collision_ops`/
`c_text_ops` sin libcalls). `color` además **verificada por demo** (`086_bob_objects`
usa `eng::util::lerp444` en el gradiente del cielo). Siguiente: R2.

### R2 — Juego avanzado (rejilla, broadphase, pathfinding)

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| R2.1 | `grid.hpp` | tile↔mundo, proyección isométrica/diamante, vecinos **hex** | HOST-097 |
| R2.2 | `broadphase.hpp` | `SpatialHash` con `HashMap`/`DirectMap` + `Pool`/`IntrusiveList` por celda | HOST-098; demo con muchas entidades |
| R2.3 | `pathfinding.hpp` | `bfs`/`dijkstra`/`astar` sobre rejilla con `PriorityQueue` + `FlatSet`/`FlatMap` y `scratch` | HOST-099; demo 110/111 |

Dependencias: R2.2/R2.3 se apoyan en el vocabulario ya entregado (contenedores, no
estructuras nuevas). Cierre de R2: demo consumidora (broadphase y/o pathfinding) y
regresión verde; ambas pasan a verificadas por demo.

**Estado: R2 completa** (`grid.hpp` HOST-097, `broadphase.hpp` HOST-098,
`pathfinding.hpp` HOST-099; sondas `c_grid_ops`/`c_broadphase_ops`/`c_pathfinding_ops`
sin libcalls). `broadphase` y `pathfinding` **verificados por demo** (`110_ylimited_shooter`
ejecuta un self-test de ambos en `init`).

### R3 — Audio y efectos

**Estado: R3 completa.** R3.1 distribuciones (`core/random.hpp`, HOST-100; `next_mod` sin
división), R3.2 worley/turbulence/ridged (`core/noise.hpp`, HOST-101) y R3.3 `dsp.hpp`
(Adsr/OnePole/DelayLine/soft_clip/osciladores, HOST-102); sonda `c_dsp_ops` sin libcalls.

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| R3.1 | `random` distribuciones | `next_range`, `float01`, `pick`, `shuffle(Span)` (Fisher-Yates), `gaussian` | HOST-100 |
| R3.2 | `noise` variantes | `worley`/cellular y `turbulence`/`ridged` (reutiliza el hash de celda) | HOST-101 |
| R3.3 | `dsp.hpp` | `ADSR`, `one_pole_lowpass/highpass`, osciladores (`sine_table`), `delay` (`RingBuffer`), `gain`/`mix`/`soft_clip` | HOST-102; demo de audio (081/066) |

### R4 — Datos y patrones «tipo Boost»

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| R4.1 | `bitstream.hpp` + `dynamic_bitset.hpp` | `BitReader`/`BitWriter`, bitset que crece con `Allocator` | **HOST-121/122** (entregado) |
| R4.2 | `variant.hpp` | unión etiquetada sin heap, `visit` con overload set | HOST (siguiente libre) |
| R4.3 | `event.hpp` | array fijo de `FunctionRef`, `subscribe`/`emit` | **HOST-109** (entregado) |
| R4.4 | `state_machine.hpp` | estados/eventos/tabla `constexpr` | **HOST-108** (entregado) |
| R4.5 | `type_list.hpp` (opcional) | `TypeList` + `for_each_type` (registro en compile-time) | HOST (siguiente libre) |

> Los pasos sin implementar usan el **siguiente `HOST-NNN` libre** en el momento de
> implementarse (hoy 129 en adelante; 000–128 están asignados). R4.3/R4.4 se adelantaron
> para desbloquear G2 (decisión/blackboard) de la IA.

R4.2–R4.5 solo si aparece consumidor (comandos/eventos/efectos). R4.5 es avanzado y se
puede posponer sin bloquear el resto.

### R5 — Extras de juego (con consumidor)

Selección de utilidades clásicas valoradas para un engine «estilo Amiga 500». Cada una se
implementa **solo con consumidor real**; el orden es por valor/coste.

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| R5.1 | `lru_cache.hpp` | generalizar el patrón de `chunk_cache.hpp` (`K`,`V`,`N`, sin heap) para tiles/sprites/mapas | **HOST-127** (entregado; falta demo que la consuma) |
| R5.2 | `interval.hpp` | `Interval`/`IntervalSet<N>`/`IntervalMap` para rangos (streaming, buffs/daño, animación) | HOST |
| R5.3 | `task.hpp` (coroutine *stackless*) | tarea con estado en `struct` y `step()` (patrón `switch`); secuencias/scripting sin corrutinas C++20 | **HOST-128** (entregado; falta secuencia de demo) |
| R5.4 | `variant.hpp` (R4.2) | unión etiquetada sin heap con `visit`, para colas de comandos/mensajes heterogéneos | HOST + consumidor |
| R5.5 | `stable_heap.hpp` / heap d-ario | *open set* de A* con *decrease-key* (índice) o heap 4-ario; **medir** antes de adoptar | HOST + medición en A* |
| R5.6 | `bloom.hpp` | filtro de Bloom fijo (bitset + k hashes) para «visitados» grandes (GOAP) | HOST |
| R5.7 | `trie.hpp` | trie / *prefix map* para autocompletado y búsqueda por prefijo | HOST + consumidor (consola) |
| R5.8 | `grid_view.hpp` (mdspan) | vista multidimensional sobre `Span` para rejillas de nivel | HOST |

> Prioridad práctica: R5.1 (cachés), R5.2 (rangos) y R5.3 (tareas) tienen consumidor claro
> (streaming, buffs/animación, secuencias); R5.4 en cuanto aparezca una cola de comandos;
> R5.5 exige **medición** (el heap binario actual puede ganar con claves pequeñas);
> R5.6–R5.8 solo si aparecen conjuntos de visitados grandes, consola de comandos o código de
> rejillas que lo pida. `type_list` (R4.5) sigue pospuesto.

## 5. Dependencias entre fases

```
   R1 (stats/color/collision/text)  ──►  R2 (grid/broadphase/pathfinding)
        │                                     │
        │                                     └─ reutiliza PriorityQueue/FlatSet/HashMap/Pool
        ▼
   R3 (random/noise/dsp)  ──►  R4 (bitstream/variant/event/fsm)
        └─ audio demos            └─ consumidor de patrones (comandos/eventos)
```

R1 no depende de nada nuevo. R2 se apoya en contenedores ya entregados. R3 es
independiente de R1/R2 (salvo `RingBuffer` para `delay`). R4 es el más prescindible. R5 se
implementa **por consumidor y en cualquier orden** una vez cerradas R1–R4.

## 6. Riesgos y decisiones abiertas

- **Estadística con `Fixed`**: `variance`/`mean` necesitan división; usar `div_norm` y
  documentar los límites de rango (saturación). Decidir si se ofrece o se limita a
  escalares con división (`float`/`double`/MF) y fixed con `div_norm`.
- **`median`/`percentile`**: requieren copiar y ordenar (no hay `nth_element` in-place sin
  mutar). Decidir entre copiar a `scratch` del llamador o documentar que mutan la vista.
- **`Broadphase`**: cuántas celdas/memoria; usar `DirectMap` para rejillas densas o
  `HashMap` para dispersas. Definir presupuesto y estrategia de listas por celda.
- **`pathfinding`**: tamaño del `scratch` (open/closed/come-from) y coste por frame; acotar
  con presupuesto o ejecutarlo en `eng::task::BackgroundQueue`.
- **`Variant`** (R4.2): **diferido por falta de consumidor** (regla §1.6 de `AGENTS.md` y
  `TEMPLATE_LIBRARY` §4). Candidatos a justificarlo: cola de comandos de juego con carga
  heterogénea o mensajes entre sistemas. Hasta entonces basta el despacho estático
  (conceptos).
- **Interner de cadenas**: **implementado** (`string_interner.hpp`, HOST-124). Deduplica por
  contenido (`HashMap<StringView,u16>`) y copia los bytes en una arena (`Allocator`) que
  aporta el llamador; pensado para nombres construidos en runtime (assets/config, etiquetas).
- **Heurística parametrizable**: **implementada** en `pathfinding.hpp`: `astar` acepta
  `detail::ChebyshevH`/`detail::EuclideanH` (Manhattan por defecto, la óptima en 4 vecinos;
  las otras son para mallas con diagonal). HOST-099.
- **Grafo genérico**: **implementado** (`graph.hpp`, HOST-126); `ai::navigation::WaypointGraph`
  ya se apoya en él. `navmesh_lite` mantiene su malla de polígonos/portales.
- **SAT 2D**: **implementado** en `collision.hpp` (`convex_overlap`/`point_in_convex`,
  HOST-125). Cubre la colisión de polígonos convexos 2D; `GJK`/`EPA` se descartan para 2D
  (sobran) y **no hay colisión 3D**: el soporte 3D actual (`linalg`/`mesh3d`/`lib3d`) es de
  modelo, transformación y render, no de física.
- **Autómata celular**: candidato menor (hoy hay simulaciones ad-hoc, p. ej. el fuego de
  HOST-015).
- **Alcance de R4**: `type_list` solo si un registro en compile-time aporta valor real.

## 7. Cómo se cierra cada paso

1. Cabecera en `engine/include/eng/core/util/` (o `eng/core/`) con comentario didáctico.
2. `tests/host/HOST-NNN` + `README.md`.
3. Sonda de codegen cuando aplique.
4. Actualizar `TEMPLATE_LIBRARY.md` (inventario, tests, estado) y este roadmap (fase hecha).
5. Commit atómico por paso, con la referencia a la fuente y a los tests.
