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
`partial_sort`, `radix_sort_u16`), `hash` y sondas de codegen. Verificados **por demo**:
`BitSet`, `StaticVector`, `RingBuffer`, `FlatMap`, `DirectMap`, `IntrusiveSList`, `Pool`,
`HashMap`.

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

### R2 — Juego avanzado (rejilla, broadphase, pathfinding)

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| R2.1 | `grid.hpp` | tile↔mundo, proyección isométrica/diamante, vecinos **hex** | HOST-097 |
| R2.2 | `broadphase.hpp` | `SpatialHash` con `HashMap`/`DirectMap` + `Pool`/`IntrusiveList` por celda | HOST-098; demo con muchas entidades |
| R2.3 | `pathfinding.hpp` | `bfs`/`dijkstra`/`astar` sobre rejilla con `PriorityQueue` + `FlatSet`/`FlatMap` y `scratch` | HOST-099; demo 110/111 |

Dependencias: R2.2/R2.3 se apoyan en el vocabulario ya entregado (contenedores, no
estructuras nuevas). Cierre de R2: demo consumidora (broadphase y/o pathfinding) y
regresión verde; ambas pasan a verificadas por demo.

### R3 — Audio y efectos

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| R3.1 | `random` distribuciones | `next_range`, `float01`, `pick`, `shuffle(Span)` (Fisher-Yates), `gaussian` | HOST-100 |
| R3.2 | `noise` variantes | `worley`/cellular y `turbulence`/`ridged` (reutiliza el hash de celda) | HOST-101 |
| R3.3 | `dsp.hpp` | `ADSR`, `one_pole_lowpass/highpass`, osciladores (`sine_table`), `delay` (`RingBuffer`), `gain`/`mix`/`soft_clip` | HOST-102; demo de audio (081/066) |

### R4 — Datos y patrones «tipo Boost»

| Paso | Entrega | Detalle | Verificación |
|---|---|---|---|
| R4.1 | `bitstream.hpp` + `dynamic_bitset.hpp` | `BitReader`/`BitWriter`, bitset que crece con `Allocator` | HOST-103 |
| R4.2 | `variant.hpp` | unión etiquetada sin heap, `visit` con overload set | HOST-104 |
| R4.3 | `event.hpp` | array fijo de `FunctionRef`, `subscribe`/`emit` | HOST-105 |
| R4.4 | `state_machine.hpp` | estados/eventos/tabla `constexpr` | HOST-106 |
| R4.5 | `type_list.hpp` (opcional) | `TypeList` + `for_each_type` (registro en compile-time) | HOST-107 |

R4.2–R4.5 solo si aparece consumidor (comandos/eventos/efectos). R4.5 es avanzado y se
puede posponer sin bloquear el resto.

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
independiente de R1/R2 (salvo `RingBuffer` para `delay`). R4 es el más prescindible.

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
- **`Variant`**: conjunto fijo de alternativas y `visit` sin RTTI; decidir si se necesita o
  basta el despacho estático (conceptos).
- **Alcance de R4**: `type_list` solo si un registro en compile-time aporta valor real.

## 7. Cómo se cierra cada paso

1. Cabecera en `engine/include/eng/core/util/` (o `eng/core/`) con comentario didáctico.
2. `tests/host/HOST-NNN` + `README.md`.
3. Sonda de codegen cuando aplique.
4. Actualizar `TEMPLATE_LIBRARY.md` (inventario, tests, estado) y este roadmap (fase hecha).
5. Commit atómico por paso, con la referencia a la fuente y a los tests.
