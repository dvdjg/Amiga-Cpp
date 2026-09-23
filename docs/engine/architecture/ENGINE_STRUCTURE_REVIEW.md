# Revisión de la estructura del engine y decisiones aceptadas

Análisis del estado de la estructura del engine y **decisiones de consolidación** adoptadas, con
lo ya ejecutado y lo que queda pendiente. Es la fuente única para no rediscutir el rumbo: describe
el estado **vigente** y el objetivo, no la historia.

## 1. Veredicto

La base está **bien fundamentada**: la separación por capas se respeta de verdad y el sistema de
tipos protege la frontera de memoria. La deuda no está en los cimientos sino en la **granularidad**
de algunos ficheros y en **conceptos solapados** (varios rectángulos, varias formas de "dónde
dibujo", un backend muy grande).

Evidencia medida en el repositorio:

- **Capas limpias**: `eng/core/` no incluye `graphics/field/scene/platform`, y `field/` no incluye
  `sim/ai/board/cards`. El núcleo nunca conoce a las capas de abajo.
- **Tipado fuerte** en la frontera: `PlaneBytes`/`MaskBuffer`/`ChunkyView` (`Bytes<Tag>`) evitan
  pasar un bitmap donde va una máscara.
- **Contratos en compilación** (`concept GameModule`/`GameIdle`) y **header-only + un backend**.
- **Disciplina de evidencia**: un test HOST por API, una demo por feature, `DOC-MAP`, numeración.
- **Headers grandes**: `xlimited.hpp` (1864 líneas), `sim/world.hpp` (1231), `field/playfield.hpp`
  (888), `scene/actor.hpp` (772), `graphics/composition/compose.hpp` (753), `field/xlimited_scene.hpp`
  (752), `graphics/drivers/tile_scroll.hpp` (735). El backend `amiga.cpp` tenía 1269.
- **Cuatro rectángulos** con convenciones distintas: `eng::Rect` (linalg), `field::ClipRect`
  (inclusivo `x1/y1`), `field::SurfaceRect` (`s32` + `w/h`), `graphics::DirtyRect` (exclusivo).
- **Acoplamiento puntual**: 3 ficheros de demo usaban tipos anidados del backend
  (`AmigaBackend::C2p4State`/`LineEorParams`/`OrBobEntry`); `eng/api/` no existía pese a estar
  documentado como objetivo.
- **Ciclo de includes**: `Playfield` declaraba `rasterize_c2p` y su definición vivía en `raster.hpp`
  (que define `Rasterizer`), porque `Playfield` no podía conocer el seam de C2P.

## 2. Decisiones aceptadas

### D1 — `DrawTarget`: objetivo de dibujo único (hecho)

`eng::field::DrawTarget` (`field/draw_target.hpp`) agrupa `Surface` + `Rasterizer` + `FramePlan` +
clip. Es la puerta única a las primitivas (`fill`/`line`/`frame`/`text`/`blit`/`c2p`). Con él,
`Playfield` deja de conocer el seam de C2P: se elimina `Playfield::rasterize_c2p` y su definición
en `raster.hpp` (**el ciclo desaparece**). `Scene::draw_target(plan)` lo expone y `Scene::c2p`
delega en él. Verificado por **HOST-232** y migrando la demo **077** (líneas por Blitter) sin
cambiar su comportamiento.

### D2 — `eng::Box`: un solo rectángulo de píxeles (hecho)

`eng/core/types/box.hpp` define `Box` (16 bits, `x/y/w/h`, `contains`/`inset`/`intersect`/`merge`/
`overlaps`/`translate`). Los tipos existentes **no se borran** (los usa el engine) pero se
convierten a/desde `Box` en su capa: `surface_rect_of`/`box_of` (`SurfaceRect`), `clip_rect_of`/
`box_of` (`ClipRect`), `dirty_rect_of`/`box_of` (`DirtyRect`). Verificado por **HOST-231**
(round-trip y bordes). La migración de llamadas existentes a `Box` es incremental.

### D3 — Familia de playfields en vez de un god-header (hecho)

`field/playfield.hpp` se parte en `playfield_base.hpp` (tipos compartidos + `Playfield`),
`canvas_playfield.hpp` (`CanvasPlayfield`) y `contiguous_playfield.hpp` (`ContiguousPlayfield`).
`playfield.hpp` queda como **cabecera de familia** que incluye las tres, así que ningún consumidor
cambia. Verificado por la suite host completa (218 tests) y builds de demo.

### D4 — `eng/api/`: fachada pública (hecho)

`eng/api/api.hpp` es un solo `#include` que reúne las cabeceras estables (bucle/contrato de juego,
escena, dibujo, rasterizado, paleta, entrada, tareas de fondo, valores preparados de Blitter) sin
definir tipos nuevos (no duplica la verdad). **No** incluye el backend. Verificado por **HOST-233**.

### D5 — Tipos preparados de Blitter al dominio (hecho)

`eng/graphics/blitter_state.hpp` reúne `graphics::OrBob`, `graphics::LineEor` y `graphics::C2p4`;
el backend Amiga los **aliasa** (`using C2p4State = eng::graphics::C2p4;`, etc.) y sigue siendo
quien los rellena y consume. Las demos **080**, **116** y **204** ya usan los nombres de dominio y
no `AmigaBackend::…`. Los campos reflejan lo que el backend precalcula (p. ej. registros
`BLTCONx`), de modo que el layout es estable y no se recompone la aritmética cada frame.

### D6 — `BlitJob` por tipo y `frame_plan.hpp` partido (hecho)

`BlitJobKind`/`BlitSource`/`BlitDest`/`BlitJob` viven ahora en `graphics/blit_job.hpp` (separado
de `frame_plan.hpp`, que queda con el plan, el presupuesto y los dirty rects). Los campos
específicos de cada operación se agrupan en sub-structs: `job.line` (`x0..y1`, `row_bytes`,
`base`) para `Line`/`LineEor` y `job.c2p` (`chunky`, `planes`, `plane_stride`, `bytes`) para
`C2P`; los campos comunes (fuente/destino, tamaño, módulos, planos, minterm) siguen en el nivel
superior. Verificado por la suite host y builds de 080/116.

### D7 — Desambiguar «scene» (hecho)

El directorio `graphics/composition/` pasa a `graphics/composition/` y el espacio de nombres
`eng::graphics::composition` a `eng::graphics::composition`: así «scene» designa una sola cosa (el
`eng::scene` de actores/escena virtual) y la composición de display queda con su nombre. Se
actualizaron includes, nombres cualificados y documentación; la suite host y los builds de demo
siguen verdes.

### D8 — Backend troceado por servicios (hecho)

`amiga.cpp` (1269 líneas) se divide en `amiga.cpp` (core: boot, memoria,
servicios de IRQ, display/copper), `amiga_blitter.cpp` (ejecución del `FramePlan` y
operaciones de Blitter) y `amiga_c2p.cpp` (las 13 fases del C2P), más
`amiga_internal.hpp` con los helpers compartidos (registros custom, espera de Blitter,
regiones, globals de IRQ) marcados `inline`. El build globa `engine/src/**/*.cpp`, así que no hay
que registrar las unidades. Verificado con 077/080/116 en hardware (READY).

### D9 — Política de cabeceras: header-only por defecto, `.cpp` con criterio (aceptado)

El engine sigue **header-only por defecto** (rendimiento m68k por inline, freestanding sin
`libeng`, testabilidad host sin enlace). Se admite `.cpp` solo para unidades **no-plantilla,
frías y pesadas** (búsqueda de tablero, tick de `sim`, decodificadores, E/S) y para el backend,
en `engine/src/<área>/`. Adoptar `.cpp` en el dominio exige antes `tools/build/build-host-lib.sh`
(compila `engine/src` sin backends a `out/host-lib/libeng.a`) enlazada por `run-host-tests.sh`;
sin ella, el código movido queda sin test host. Una cabecera gigante se arregla **partiéndola
por tema**, no moviéndola a `.cpp`. Canónico en [`HEADER_POLICY.md`](HEADER_POLICY.md).

### D10 — Cabeceras de `core/` por tema (hecho)

`eng/core/` se subdivide en `math/`, `types/` y `data/` (más `util/`). Los consumidores del repo
ya usan las rutas nuevas (no quedan paraguas). Lo vigila `tools/check/engine-tree.mjs`. Plan y
árbol en [`../../guides/roadmap/PLAN_ORGANIZACION_ENGINE.md`](../../guides/roadmap/PLAN_ORGANIZACION_ENGINE.md).

### D11 — Modelo de tres anillos de plataforma (hecho)

La plataforma se separa en **anillo 0 (dominio agnóstico)**, **anillo 1 (vocabulario de chipset
por familia)** y **anillo 2 (backend por objetivo)**. Todo el vocabulario Amiga vive en
`eng/platform/amiga/` y el backend canónico es `eng::amiga::AmigaBackend` (sin alias de
compatibilidad: retirado). A1200 no es un backend distinto: es el mismo backend Amiga con otro
perfil (`HardwareProfile`) y otro target (`K_AGA`). Atari ST y Megadrive se acoplan implementando
el **contrato de backend** (`eng/platform/backend.hpp`) sin tocar el dominio; la frontera la vigila
`tools/check/platform-boundaries.mjs`. Canónico en [`PLATFORM_LAYERS.md`](PLATFORM_LAYERS.md).

### D12 — Tests por plataforma, nivel y categoría (hecho)

Los tests se organizan por **plataforma** (`host`/`amiga`/`atarist`/`megadrive`), **nivel**
(`L0`…`L3`) y **categoría (dominio)**, con un catálogo por categoría en lugar de un catálogo
único. Los IDs `HOST-NNN` se conservan (agrupar no renumera). Lo validan
`tools/check/test-numbering.mjs` (árbol anidado) y `tools/run-host-tests.sh --category`.
Taxonomía en [`../../testing/TAXONOMY.md`](../../testing/TAXONOMY.md).

### D13 — Cabeceras grandes: `compose.hpp` troceado; god-classes pendientes (parcial)

`graphics/composition/compose.hpp` (927 líneas) se parte en `scene.hpp` (la clase `Scene`) y
`stages.hpp` (etapas/presets y huellas `*_words`), con `compose.hpp` como **cabecera de familia**
(patrón de D3). En cambio, `field/xlimited_playfield.hpp` y `sim/world.hpp` son **una sola clase**
cada uno: trocearlos por tema exige un refactor de clase (base + mixins, o definiciones fuera de
clase) con su propia verificación en hardware. Se dejan como están, señalados por
`tools/check/header-impl.mjs` (advisory), y no se fuerzan sin plan y evidencia (ver §3).

## 3. Límites de esta revisión (excepciones deliberadas)

- **God-classes no troceadas**: `field/xlimited_playfield.hpp` y `sim/world.hpp` son una sola
  clase; trocearlas es un refactor grande que merece su propio plan y su propia verificación (D13).
  La fachada (D4), los tipos de dominio (D5), el troceo del backend (D8) y el de `compose.hpp`
  (D13) ya reducen el acoplamiento visible.
- **`graphics::LineEor`/`C2p4` exponen campos que el backend precalcula.** Es deliberado: ocultarlos
  tras un handle opaco obligaría a cambiar las firmas del backend y las rutas calientes de 116
  (batch de líneas por plano) y 080 (C2P encadenado por IRQ de blit), con riesgo de regresión. Se
  documentan como **valores preparados por el backend**, no como API de alto nivel.

## 4. Regla de mantenimiento

- Un hecho, un sitio: no se crean segundos tipos/verdades; si algo se unifica, los adaptadores van
  en la capa que ya conocía el tipo antiguo.
- La fachada `eng/api/api.hpp` es la puerta del juego; el backend se instancia en `main()`.
- Al añadir un tipo de dominio preparado por el backend, se declara en `eng::graphics` (o la capa
  de dominio que corresponda) y el backend lo **aliasa**; nunca se nombra `AmigaBackend::X` en
  la lógica de demo.
