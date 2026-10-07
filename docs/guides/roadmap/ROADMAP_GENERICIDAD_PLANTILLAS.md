# Roadmap: genericidad de plantillas (sin tipos de dato concretos)

**Estado: cerrado.** Todos los ítems están hechos o descartados con motivo (ver la columna Estado); la deuda aceptada por el tope de tipos crudos está triada (defaults de compatibilidad de parámetros ya genéricos o tipos de dominio/formato/ABI). La evidencia de la pasada está al final.

Plan para que las plantillas del engine que valen para varios tipos **no queden atadas a uno concreto** (`s32`, `s16`, `u16`, `u8`, `float`…): el tipo que varía se declara como **parámetro de plantilla** o **punto de extensión**, y el comportamiento propio de una representación se aporta como **especialización explícita**, no con ramas internas. Regla vigente: `AGENTS.md` §1.11 y «Reglas obligatorias de diseño» de [`CODING_STYLE.md`](../../engine/architecture/CODING_STYLE.md).

```
   tipo concreto detectado en una plantilla genérica
        │
        ├─ ¿el tipo es dominio fijo (píxel, registro, ABI, layout)? ──► se deja, documentado
        │
        ├─ ¿el algoritmo varía con el tipo y cabe en la firma? ───────► parámetro de plantilla
        │                                                              (con defecto si no rompe usos)
        ├─ ¿necesita vocabulario del tipo (cero, conversión, ancho)? ──► punto de extensión
        │                                                              (scalar_traits, numeric_traits,
        │                                                               scalar_const, arith<Repr>…)
        └─ ¿solo algunos tipos tienen comportamiento propio? ─────────► especialización explícita
                                                                       por tipo (patrón del repo:
                                                                       scalar_div<Fixed<…>>,
                                                                       scalar_sqrt<Fixed<…>>)
```

Ejemplo canónico: un generador de seno debe poder emitir la muestra como `u8`, `s16`, `Fixed`/q12 o `float`; el antipatrón es `SineTable<s32 Amp, u32 Steps>`, que siempre materializa `s32`.

## Estado de la detección

- Gate vigente: `node tools/check/generic-headers.mjs`. Cubre solo las representaciones conocidas (`Fixed<s16…>`, `MiniFloat16`, `q0/q8/q12/q24`), los alias internos a ellas y los `#include` de escalar/retro. **No** ve tipos crudos (`s16`/`s32`/`u16`/`u8`/`float`) ni patrones como `ct_array<s32,…>`, pero ya los **avisa** de forma no bloqueante (F4.4).
- Inventario de la pasada de revisión: abajo, ordenado por valor de uso. Los tipos de **dominio** (píxel, tile, registro, ABI) y las capas exentas (`retro/`, `platform/`, `cpu/`, `field/`, `fixed*`, `minifloat*`) quedan fuera por diseño.

## Piezas compartidas de esta pasada

- `eng::util::sat_add<Cost>` (`core/util/intmath.hpp`): suma saturada de costes (A\* de grafo/rejilla y campo de flujo), sin libcalls.
- `eng::util::make_signed_t<T>` / `signed_max<T>` (`core/util/type_traits.hpp`): predecesor con signo del índice (`-1` = no visitado) y su cota de capacidad.
- `eng::util::type_identity_t<T>` (`core/util/type_traits.hpp`): bloquea la deducción de `Index`/`Cost` donde el valor debe venir de la capacidad o del defecto, no de un literal `0u`.
- `scalar_traits<Fixed>::wide_t`/`to_wide`/`from_wide`/`wide_div` (`core/math/fixed.hpp`): acumulador ancho declarado por el escalar, con saturación al estrechar y `divs.w` nativo en 68000.
- `eng::detail::sine_sample<T>` (`core/math/sinetable.hpp`) y su especialización `Fixed` (`core/math/fixed_math.hpp`): conversión de muestra de `SineTable` como punto de extensión.

## Fase 1 — Generadores y tablas (representación de salida)

| # | Sitio | Atadura | Propuesta | Estado |
|---|---|---|---|---|
| 1.1 | `engine/include/eng/core/math/sinetable.hpp` (`SineTable`) | salida siempre `s32` (`ct_array<s32,…>`, `sample() -> s32`) | `template <s32 Amp, u32 Steps = 64, class T = s32, s32 Offset = 0>`; conversión de muestra por punto de extensión `detail::sine_sample<T>` (aritméticos por `static_cast`; `Fixed` por especialización en `fixed_math.hpp`); `Offset` cubre los tipos sin signo centrados (p. ej. `u8` con 128) | hecho |
| 1.2 | `engine/include/eng/audio/wave_tables.hpp` | `sine_byte`/`synth_tone`/`synth_sequence` solo 8 bits (`s8`/`u8`) | `sine_wave<T>`/`synth_tone<Len,T>`/`synth_sequence<Len,T>` con `wave_traits<T>` (pico y tipo con signo; `u8` de Paula con signo interpretado, `s16` PCM); el seno se genera en compilación. `triangle_byte`/`square_byte` siguen 8-bit (formas de control) | hecho |

## Fase 2 — Índices y costes (quitar el techo estructural de 16/32767 bits)

| # | Sitio | Atadura | Propuesta | Estado |
|---|---|---|---|---|
| 2.1 | `engine/include/eng/core/util/graph.hpp` (`Graph`, `graph_*`) | `Graph` con nodos/costes `u16`; `came_from` `s16` | `template <u16 MaxNodes, u16 MaxEdges, class Index = u16, class Cost = u16>`; `no_node = ~Index{0}`; `came_from` en el entero con signo correspondiente a `Index` | hecho |
| 2.2 | `engine/include/eng/core/util/pathfinding.hpp` (`bfs`/`astar`/`reconstruct_path`) | índice `u16` (límite 32767 por `came_from` `s16`), coste `u16` | `Index` (y `Cost` en `astar`) como parámetros con defecto; heurísticas parametrizadas por el tipo de coste | hecho |
| 2.3 | `engine/include/eng/ai/navigation/flow_field.hpp` (`compute_flow_field`) | integración/coste `u16` (0xffff bloqueada = inalcanzable) | `template <u16 W, u16 H, class Index = u16, class Cost = u16>`; sentinela por el máximo del tipo de coste | hecho |
| 2.4 | `engine/include/eng/core/util/broadphase.hpp` (`SpatialHash`) | id `u16` fijo (la posición es dominio: `s16`/`Aabb`) | `Id` como parámetro con defecto `u16`; la posición se deja `s16` (colisión de pantalla/tile) | hecho |

## Fase 3 — Numéricos con representación o promoción fija

| # | Sitio | Atadura | Propuesta | Estado |
|---|---|---|---|---|
| 3.1 | `engine/include/eng/core/util/stats.hpp` (`sum`/`mean`) | la ruta ancha accede a `x.v`, `sat_s16` y `div_wide` (asume `Fixed<s16>`) | rasgos declarados en `scalar_traits` (`wide_t`, `to_wide`, `from_wide`, `wide_div`); sin tocar miembros internos desde `stats` | hecho |
| 3.2 | `engine/include/eng/core/math/light.hpp` (`shade_portable`) | clamp 511 fijo | clamp por `Table::size()` (511 si el adaptador no expone tamaño); el pipeline `hi16`/`mulu` es el ABI 16 bits de lib3d | hecho |
| 3.3 | `engine/include/eng/core/data/polygon.hpp` (`convex_spans`) | `Span<const s32>` para xs/ys | `template <class T>` (entero con signo) | hecho |
| 3.4 | `engine/include/eng/core/math/inv_sqrt.hpp` | `R` sin signo sin comprobar | `static_assert` de entero sin signo (la tabla es una magnitud) | hecho |
| 3.5 | `engine/include/eng/core/data/mesh3d.hpp` | `mesh_traits::key = s16`; `face_signed_area -> s32` | **se deja como está** (decisión): `mesh3d` es vocabulario retro (lib3d, orden Z de 16 bits) y no es del todo reutilizable fuera de ese camino; `mesh_traits<S>` ya es especializable si un consumidor lo necesita | descartado (documentado) |
| 3.6 | `engine/include/eng/ai/perception/influence_map.hpp` | valores `s32` fijos | `template <u16 W, u16 H, class T = s32>` (entero con signo); decay satura al cero | hecho |

## Fase 4 — Resto (conveniencia y deuda menor)

| # | Sitio | Atadura | Propuesta | Estado |
|---|---|---|---|---|
| 4.1 | `engine/include/eng/graphics/mesh_renderer.hpp` | pixels de pantalla `s16` y `MeshView` por defecto | **se deja como está** (decisión): es el adaptador de dominio de `Surface`; las piezas genéricas (`mesh3d`, `project_perspective<S>`) ya lo son; nota del fichero actualizada | descartado (documentado) |
| 4.2 | `engine/include/eng/scene/trajectory.hpp` (`gen_line`/`gen_sine_vertical`) | solo `PathPoint<s16>` | `template <class S>` (deducido de `Span<PathPoint<S>>`; los usos s16 no cambian) | hecho |
| 4.3 | `engine/include/eng/core/util/interval.hpp`, `quantizer.hpp` | extremos `s32`; scratch interno de 64 niveles | `IntervalT<T>`/`IntervalSet<N,T>` (alias `Interval = IntervalT<s32>`) y `lloyd_max<S, MaxLevels>` | hecho |
| 4.3b | `engine/include/eng/core/math/random.hpp`, `ai/navigation/navmesh_lite.hpp` | salidas `s32` del PRNG; coste de portal `u16` | **se dejan** (decisión): el ancho de palabra del PRNG es su contrato de rango, y el coste de portal es dominio de navegación (máscara de terreno `u16`) | descartado (documentado) |
| 4.4 | `tools/check/generic-headers.mjs` | solo detectaba representaciones conocidas | tope de tipos crudos por fichero (`generic-headers-raw-baseline.txt`, solo puede bajar; fichero nuevo con usos falla) | hecho (tope triado: defaults de compatibilidad o dominio/ABI) |

## Verificación por fase

- Test host de la cabecera con **dos tipos distintos** además de los casos límite (regla §1.11): p. ej. `u8` + `Fixed<s16,12>` para el seno, `Index=u8` + `Index=u16` para el grafo/rejilla, `Fixed<s32>` además de `Fixed<s16>` para `stats`.
- Gates en cada pasada: `node tools/check/{generic-headers,encoding,links,doc-index}.mjs`.
- Sin regresión de comportamiento: los tipos por defecto conservan el contrato actual (mismos valores y mismas firmas donde ya se instanciaba).
- Demos afectadas: build + regresión de las que usan la utilidad tocada (083/086/110/201/202/275, rotozoom, `route_camera`).

## Validación de la pasada (evidencia)

- Tests host: HOST-009 (ondas), 013 (polygon), 020 (seno), 047/054 (light/lib3d), 093 (stats), 098 (broadphase), 099/114/126/116 (grafo/rejilla/campo/camino), 104 (fixed_math), 129 (interval), 154/158/164/166/249 (sim/crowd), 380 (Lloyd-Max) en verde.
- m68k: demos 067 (audio), 085 (copper/`SineTable`), 110 (playfield/`bfs`), 116 (light/polygon) compilan; `codegen-report` sin libcalls ni 68020+.
- Visión (Ollama, `DEMO_VISUAL_DEBUG`): 085 descripción limpia; en 110 la sospecha de «columnas desplazadas/parpadeo» quedó **refutada por los gates objetivos** (secuencia animada sin pares duplicados, regresión de flicker OK, `camX=0` fijo → borde de mapa, no artefacto).
- Commits de la pasada: `010c6516`, `9fceabb0`, `5b2e89ef`, `9ecf1faa`, `5ea80cff` (regla/roadmap, SineTable, índices, stats, gate) y `9a855391`, `50d7c83c`, `964203e8` (F2/F3, gates saneados, ondas).

## Fuera de alcance (tipos de dominio que se dejan)

`core/util/collision.hpp`/`grid.hpp` (coordenadas de pantalla/tile), `graphics/effects/rotozoom.hpp` (16.16 con layout asm), `graphics/animation.hpp` (píxeles), registros/paletas, `retro/`/`cpu/`/`platform/`/`field/` y las ABI binarias (`object3d`/lib3d). La regla §1.11 no los alcanza: su tipo concreto es el dominio.
