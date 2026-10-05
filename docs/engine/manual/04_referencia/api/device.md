# Referencia — `device` (`eng/api/device.hpp`)

`eng::Device` es el **escape de hardware agrupado** (nivel B): memoria, Blitter, Copper y raster, para
que `App` quede corto y el juego pida hardware **por intención** sin nombrar el backend. Se obtiene con
`app.device()`. Cada método reenvía al backend si lo soporta (`requires`); si no, es no-op (`false`). Un
juego simple **ignora** `device()`.

## Memoria

| Método | Firma | Devuelve |
|---|---|---|
| `memory_manager` | `auto& memory_manager()` | los **bancos tipados** del backend (`chip().reserve<Tag>(…)`). |

## Blitter

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `wait_blitter` | `bool wait_blitter()` | — | `true` si esperó. |
| `install_raster` | `bool install_raster(Scene&)` | la escena | `true` si instaló el rasterizador por Blitter. |
| `blitter_clear` | `bool blitter_clear(PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes, u16 w, u16 h, bool wait = true)` | destino + geometría | `false` si el backend no lo soporta. |
| `blitter_or_bobs` | `bool blitter_or_bobs(Span<const OrBob> bobs, u16 words, u16 height, s16 src_mod, s16 dst_mod)` (y variante de un solo `OrBob`) | lote + módulos | `false` si no aplica. |
| `blitter_collide` | `bool blitter_collide(a, b, scratch, planes, row_bytes, plane_bytes, words, rows)` | dos planos + scratch | `true` si hay **algún** bit en `a & b` (colisión pixel-perfect). |
| `execute_frame_plan` | `bool execute_frame_plan(FramePlan&)` | el plan del frame | `false` si el backend no lo ejecuta. |

## Copper

| Método | Firma | Devuelve |
|---|---|---|
| `takeover_copper` | `bool takeover_copper(copper::Plan&)` | `true` si instaló el programa (una vez, en `init`). |
| `commit_copper` | `bool commit_copper(copper::Plan&)` | `true` si publicó el bloque inactivo (swap de `COP1LC`, tras VBlank). |

## Escena y Copper de alto nivel

| Método | Firma | Devuelve |
|---|---|---|
| `scene` | `Scene& scene()` | la escena ligada. |
| `copper` | `copper::Plan& copper()` | el programa de Copper de la escena. |
| `copper_scheduler` | `copper::Scheduler& copper_scheduler()` | el emisor. |
| `copper_builder` | `Copper copper_builder()` | la [fachada `Copper`](copper.md) sobre el scheduler. |

Todos los métodos son **templates** sobre el backend: se instancian al usarlos, así que no exigen nada
a un backend que no los tenga.

> Fuente: `engine/include/eng/api/device.hpp`. Diseño: `docs/engine/architecture/ROADMAP_API_COHERENCE.md` (F2).

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
