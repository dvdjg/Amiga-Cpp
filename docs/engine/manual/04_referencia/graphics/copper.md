# Referencia — Copper

El Copper es un **procesador de vídeo** que ejecuta una lista de `MOVE`/`WAIT` sincronizada al raster. Programa los registros del chipset (punteros de bitplane, paleta, módulos, prioridades) en la línea exacta. Este grupo va del emisor mínimo (`ListBuilder`) al planificador por capas (`Plan`) y al doble buffer.

```
   capas / efectos                     Plan (no posee la lista)              Copper
   ───────────────                     ───────────────────────              ──────
   graphics::CopperIntent ──add()──► [ intents (SIN ordenar) ]
                                          │ materialize(): ordena por línea
                                          ▼
   Scheduler / ListBuilder ─────────► buffer TRASERO ──commit()──► COP1LC (swap en VBlank)
   (CPU rellena el trasero;            el Copper ejecuta el frontal)
```

## Emisor mínimo — `copper.hpp`

`Register` (`copper.hpp:35`) es el enum de registros custom; `DmaControl` (`copper.hpp:97`) los bits de DMACON. `ListBuilder` (`copper.hpp:149`) sabe escribir instrucciones: `move(reg, value)`, `move32`, `move_bitplane_pointer(plane, address)`, `wait_line`/`wait_position` (y variantes PAL), `patch_data`/`patch_move_reg` y los `patch_move32` (re-apuntar una instrucción ya emitida). Es la capa de **sintaxis**; no ordena ni resuelve conflictos.

## `Scheduler` — `scheduler.hpp`

`SchedulerT<Report = true>` (`scheduler.hpp:93`; alias `Scheduler = SchedulerT<true>`, `:758`) es una abstracción de engine: recibe intenciones de varios sistemas y produce **una única** copperlist con métricas de coste. Lleva un `ListBuilder` + una `Timeline` (bitset de 32 B) + un `ScheduleReport` (solo si `Report`). `Report = false` omite Timeline y contadores (hot path más rápido); `retarget(...)` re-apunta a otra lista **sin copiar** la Timeline. `ScheduleReport` (`:53`) y `PatchHandle` (`:72`, handle de un MOVE parcheable) son sus tipos auxiliares.

## Coste por línea — `timeline.hpp`

`Timeline` (`timeline.hpp:48`) es el modelo didáctico de coste: el Copper comparte tiempo con el barrido de vídeo, así que escribir 32 colores fuera de la zona visible es barato, pero dentro de líneas con píxeles activos es agresivo. Los contadores **no se inicializan**: se marcan las líneas tocadas con un bitset de 32 B (lo único a cero) y `finish()`/las consultas ignoran las no tocadas (evita el coste de limpiar `2×256` bytes por frame). `TimelineReport` (`:36`) resume `moves`/`waits`/`visible_moves`/`over_budget_lines` y `has_visible_spill`. Orientativo: el presupuesto de CPU de una línea PAL es ~227 ciclos.

## `Plan` — `plan.hpp`

`Plan` (`plan.hpp:87`) es el **plan de Copper de una escena**: recolecta las intenciones de capas/efectos (tracks), las **ordena por scanline** y las materializa en el buffer trasero de un `DoubleBuffer`, que se publica con el swap de `COP1LC`. Es el eslabón entre el vocabulario portable (`CopperIntent`) y los emisores. Uso por frame: `begin_frame()` → emisión estática (`scheduler().emit_planes_display(...)`, `emit_palette(...)`) → `add(intents, n)` (sin ordenar) → `materialize()` (ordena y emite) → cola de la lista → `end_frame()` (¿overflow?) → `commit(backend)` (instala; swap en VBlank). Tipos: `PlanConfig` (`:56`), `BandScope` (`:71`), `EffectCost` (`:79`), `no_effect = 0xff` (`:85`). Ver `docs/engine/architecture/DISPLAY_COMPOSITION.md` §5.

## Doble buffer y plantilla

`DoubleBuffer` (`double_buffer.hpp:50`) posee dos bloques de copperlist: la CPU escribe el **trasero** (`inactive_words()`) mientras el Copper ejecuta el **frontal** (`active_words()`); `flip()` conmuta y `install(backend)` publica con el swap de `COP1LC` — **nunca** `COPJMP1` (no se ve una lista a medio escribir). `takeover(backend)` engancha el display una vez.

`Template` (`template.hpp:34`) construye la **estructura** de una copperlist una vez y expone *slots* para parchear por frame solo las palabras que cambian (`move_slot`, `wait_slot`, `set`, `set_wait`). En un A500, reescribir la lista entera cada frame cuesta ~decenas de ciclos por palabra (la CPU compite con el DMA): separar estructura (fija) de datos (por frame) reduce las escrituras.

## Lista estática — `static_plan.hpp`

`StaticCopperList<MaxWords>` (`static_plan.hpp:61`) materializa una lista de display **estática** (cabecera, punteros de plano, paleta, sprite blanks, `BPLCON2`…) anotando las ranuras del dato para el reloc: `emit_move` (`:108`) y `emit_plane_pointer` (`:117`, MOVE de PTH+PTL). `StaticDisplayLayout` (`:53`) describe la geometría que se materializa.

Volver al [índice de `graphics/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
