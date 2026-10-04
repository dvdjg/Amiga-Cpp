# Referencia — composición de escenas

Una escena se construye **uniendo etapas** que piden recursos y emiten Copper, en vez de una clase por driver. El **programa** es data (Copper) que ejecuta el chip; la **variabilidad** se hace con handles parcheables. Ver `docs/engine/architecture/SCENE_COMPOSITION.md`.

```
   escena = unión de ETAPAS (no una clase por driver)
   ┌──────────────────────────────────────────────────────────────┐
   │ composition::Scene                                            │
   │  recursos: geometría · planos · buffers · paleta             │
   │  ├─ display()        ─┐                                       │
   │  ├─ palette()        ─┼─► piden recursos y emiten al Scheduler│
   │  ├─ palette_zones()  ─┤                                       │
   │  └─ reverse_ptrs()   ─┘                                       │
   │  ciclo de vida: Task = FunctionRef<void()> (por frame)        │
   │  variabilidad : copper::PatchHandle (scheduler().patchable)   │
   └──────────────────────────────────────────────────────────────┘
```

## `Scene` — `composition/scene.hpp`

`Scene` (`scene.hpp:76`) es el contenedor: recursos (geometría, planos, buffers, paleta) y un `copper::Scheduler`. `Task = FunctionRef<void()>` (`:37`) es el ciclo de vida por frame. `kMaxSceneBuffers = 3` (`:47`) y `kMaxScenePlanes = 6` (`:48`) acotan buffers y planos. `Patch32` (`:58`)/`patch32_at(scheduler, hi_index)` (`:70`) localizan el par de MOVE `hi`/`lo` de un puntero de 32 bits para parchearlo barato.

## Etapas y presets — `composition/stages.hpp`

Una **etapa** es un callable `void(Scene&)`. Los **presets** devuelven un `SceneResources`. `planar(width, height, planes, …)` (`stages.hpp:38`) declara la geometría; `display(...)` (`:99`) y `display(DisplayGeometry, bplcon0)` (`:154`) emiten el display; `palette(colors, first, count)` (`:172`) emite el tramo de paleta; `palette_zones` (`:242`) y `palette_patchable` (`:271`) zonas de paleta; `patchable_zone(line, slots, …)` (`:218`) una zona de MOVEs parcheables (`PatchSlot`/`PatchZone`, `:197`); `reverse_ptrs()` (`:49`) los punteros inversos del doble buffer; `intents(list)` (`:179`) intenciones crudas. `bplcon0_for(mode, planes)` (`:80`) deriva `BPLCON0` (con las constantes `kBplcon0_*`, `:72`).

`compose(scene, memory, res, ...)` (`compose.hpp`) une las etapas sobre una escena; ver su cabecera para el uso completo.

## Validación — `composition/limits.hpp`

Expresar una configuración no garantiza que el hardware la admita (p. ej. `width = 336` en Amiga, o 5 planos por playfield en un A500 sin AGA). `limits.hpp` aporta:

| Símbolo | Qué hace |
|---|---|
| `DisplayLimits` | **Perfil de capacidades** del backend/máquina (agnóstico: describe qué admite). |
| `ocs_a500` / `ecs` / `aga_a1200` | Perfiles predefinidos como **datos**. |
| `validate(res, limits)` | Comprobación en **runtime**: primer motivo de rechazo (código + mensaje). |
| `valid_scene(res, limits)` | Comprobación en **compilación** (`consteval`), usable en `static_assert`. |
| `geometry_for(res)` / `DisplayGeometry` | DIW/DDF derivados de los recursos. |
| `dma_cost(res, limits, fw)` / `FetchWidth` | **Coste de bus** del display (informativo, no validez; `FMODE` en AGA). |

`SceneMode` (`limits.hpp:41`): `Standard`/`Ham`/`Ehb`/`DualPlayfield` + `CopperChunky`, determina qué límites aplican. Las restricciones concretas de Amiga se derivan de `docs/reference/amiga/hardware/amiga-chipset-matrix.md`.

## Copper chunky — `composition/copper_chunky.hpp`

`CopperChunkyLayer<MaxCols, MaxRows>` (`copper_chunky.hpp:53`) implementa el **display sin bitplanes**: el Copper escribe `COLOR00` por bloques de `block_h` líneas a lo largo de cada fila, re-ejecutando la misma «línea de color» vía `COP2LC`/`COPJMP2` y saliendo con `SKIP` (porte de `effects/plasma`). Sobre una escena `SceneMode::CopperChunky` (`planes == 0`): `attach(scene, {.cols, .rows})` emite la estructura una vez en ambos bloques; por frame `begin_frame(scene)` apunta al bloque inactivo, el efecto escribe colores por fila (`row(y)`) y `end_frame(scene, backend)` hace flip + install. `CopperChunkyConfig` (`:34`) recoge `cols`/`rows`/`block_h`/`first_line`/`label_hpos`/`skip_hpos`.

## `FramePlan` — `graphics/frame_plan.hpp`

`FramePlan` (`frame_plan.hpp:176`) recoge las intenciones de render de un frame **de forma portable**; el driver Amiga decide si las materializa como parches de copperlist, blits, sprites o escrituras CPU.

| Tipo | Qué es |
|---|---|
| `PalettePatch` (`:49`) | Cambio de colores RGB444 (`target` base/zona, `line`, `first`, `count`). |
| `BlitBudget`/`BlitBudgetLimits`/`BlitBudgetReport`/`BlitBudgetStatus` (`:64`) | Presupuesto de blits del frame y su informe. |
| `DirtyRect`/`DirtyReport` (`:126`) | Áreas tocadas (para restaurar/redibujar); `dirty_rect_of`/`box_of` (`:148`). |
| `ReorderPolicy` (`:116`) | Política de reordenación del plan. |

Volver al [índice de `graphics/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
