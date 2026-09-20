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
  (888), `scene/actor.hpp` (772), `graphics/scene/compose.hpp` (753), `field/xlimited_scene.hpp`
  (752), `graphics/drivers/tile_scroll.hpp` (735). El backend `amiga_minimal.cpp` tenía 1269.
- **Cuatro rectángulos** con convenciones distintas: `eng::Rect` (linalg), `field::ClipRect`
  (inclusivo `x1/y1`), `field::SurfaceRect` (`s32` + `w/h`), `graphics::DirtyRect` (exclusivo).
- **Acoplamiento puntual**: 3 ficheros de demo usaban tipos anidados del backend
  (`MinimalBackend::C2p4State`/`LineEorParams`/`OrBobEntry`); `eng/api/` no existía pese a estar
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

`eng/core/box.hpp` define `Box` (16 bits, `x/y/w/h`, `contains`/`inset`/`intersect`/`merge`/
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
no `MinimalBackend::…`. Los campos reflejan lo que el backend precalcula (p. ej. registros
`BLTCONx`), de modo que el layout es estable y no se recompone la aritmética cada frame.

### D6 — `BlitJob` por tipo y `frame_plan.hpp` partido (pendiente)

`BlitJob` mezcla campos de línea, C2P, patrón y cookie-cut; `frame_plan.hpp` acumula parches de
paleta, dirty rects, `BlitJob` y presupuesto. Objetivo: separar `blit_job.hpp` y agrupar los campos
por `BlitJobKind` (sub-structs o vistas). **Pendiente**; no se aborda aquí por ser un cambio
grande y de bajo beneficio inmediato.

### D7 — Desambiguar «scene» (pendiente)

`eng::scene` (actor, virtual_scene) y `eng::graphics::scene` (composición de copper) comparten
nombre, y hay una clase `Scene` en `compose.hpp` frente a `virtual_scene.hpp`. Objetivo: renombrar
lo de composición (p. ej. `eng::composition`). **Pendiente**; requiere tocar muchos includes y no
aporta funcionalidad.

## 3. Límites de esta revisión (excepciones deliberadas)

- **No se trocea el backend** `amiga_minimal` (display/blitter/C2P en un solo `.cpp`) ni
  `xlimited.hpp`/`actor.hpp`: es un refactor grande que merece su propio plan y su propia
  verificación. La fachada (D4) y los tipos de dominio (D5) ya reducen el acoplamiento visible.
- **`graphics::LineEor`/`C2p4` exponen campos que el backend precalcula.** Es deliberado: ocultarlos
  tras un handle opaco obligaría a cambiar las firmas del backend y las rutas calientes de 116
  (batch de líneas por plano) y 080 (C2P encadenado por IRQ de blit), con riesgo de regresión. Se
  documentan como **valores preparados por el backend**, no como API de alto nivel.

## 4. Regla de mantenimiento

- Un hecho, un sitio: no se crean segundos tipos/verdades; si algo se unifica, los adaptadores van
  en la capa que ya conocía el tipo antiguo.
- La fachada `eng/api/api.hpp` es la puerta del juego; el backend se instancia en `main()`.
- Al añadir un tipo de dominio preparado por el backend, se declara en `eng::graphics` (o la capa
  de dominio que corresponda) y el backend lo **aliasa**; nunca se nombra `MinimalBackend::X` en
  la lógica de demo.
