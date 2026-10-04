# Referencia — planar (bitmaps y blits)

La memoria gráfica del Amiga es **planar**: cada píxel se reparte bit a bit entre N planos. Este grupo describe la **zona de memoria** (`Bitmap`, `BitmapView`), su **disposición** (`PlaneLayout`) y el **trabajo de Blitter** (`BlitJob`, `BlitQueue`).

## `Bitmap` y `BitmapConfig` — `bitmap.hpp`

`BitmapConfig` (`bitmap.hpp:42`) declara la geometría (`width`/`height`/`planes`), el layout, `row_bytes` (0 = auto `width/8`), alineación, `guard_bytes` y `frontbase_offset`. `Bitmap` (`bitmap.hpp:54`) es el **dueño** de la reserva, **siempre en Chip** (el Blitter y el bitplane DMA solo alcanzan Chip; un framebuffer Fast no es mostrable):

| Método | Qué hace |
|---|---|
| `init(memory, cfg)` (`bitmap.hpp:80`) | Reserva `total_bytes + guard_bytes` en Chip; `bytes()`/`front()` apuntan a `base + frontbase_offset` (offset de fetch ancho del corkscrew: normal 0, BPL32 16, 4x 48). La guarda protege lecturas DMA/blits que rebasan el final lógico. |
| `release()` | Devuelve el bloque Chip al banco. |
| `base()`/`front()`/`chip_planes()`/`allocation_start()` | Direcciones tipadas (`Address<Chip>`, `ChipView<PlaneTag>`). |
| `bytes()` / `byte_offset(x, y, plane)` (`bitmap.hpp:126`) | Vista acotada / offset físico según el layout. |

## `BitmapView` — `bitmap_view.hpp`

`BitmapView<Tag, Bank = Chip>` (`bitmap_view.hpp:35`) es la **zona no propietaria** que viaja por las APIs (como un `Span`, pero de zona gráfica): planos como `MemView<Tag, Bank>` + `width`/`height`/`row_bytes`/`plane_count`. Es la abstracción común de todo lo que representa un área gráfica (sprites de hardware, BOBs, zonas del framebuffer, viewports); el **dueño** es un `Bitmap` o un `Block<Tag, Bank>`. El **banco en el tipo** hace que un área Chip no se pueda usar donde se exige Fast (y al revés), y permite elegir el camino (Blitter DMA vs C2P por CPU) según los tags. Sustituye al `BobTarget` (`u8*` suelto). Alias: `ChipBitmapView`/`FastBitmapView`/`SlowBitmapView` (`bitmap_view.hpp:92`).

Límites de transferencia con el Blitter OCS/ECS (AHRM cap. 6): `kBlitterMaxWordsPerRow = 64`, `kBlitterMaxRows = 1024`, `kBlitterMaxPlanes = 6`, `kBlitterCookieCutPlanes = 5` (`bitmap_view.hpp:25`).

## Disposición de planos — `plane_layout.hpp`

`PlaneLayout` (`plane_layout.hpp:16`) es el **único** enum del engine: `Contiguous` (planos uno tras otro; alias `Planar` y `Separate`) o `Interleaved` (filas de planos alternadas). Unifica los antiguos `SceneLayout` y `BobLayout`, que eran el mismo concepto con dos nombres.

## Trabajo de Blitter — `blit_job.hpp`

`BlitJobKind` (`blit_job.hpp:36`) enumera las operaciones: copias rectangulares (`CopyRect`, `RestoreRect`, `TileBlockCopy`), cookie-cut (`MaskedBobCookieCut`, `MaskedBlobNoSave`), borrado/relleno (`ClearRect`, `FillRect`), `OrBlob`, líneas (`Line`, `LineEor`), `LogicBlit`, `PatternFill` y `C2P` (13 fases).

`BlitPtr` (`blit_job.hpp:83`) es el **rol** de origen/destino/máscara: lleva `Address<MemoryKind::Chip>` (la procedencia DMA en el tipo, no un `u16*`). Un solo tipo para los tres roles — el hardware del Blitter es simétrico (A/B/C/D se intercambian) y quién es fuente lo decide el programador, no el tipo. Se construye desde un `ChipView<Tag>` o, de forma **explícita**, con `from_storage` (`blit_job.hpp:92`).

`BlitJob` (`blit_job.hpp:120`) agrupa los campos **comunes** (fuente/destino/máscara, tamaño, módulos, planos, minterm) y sub-structs por operación (`job.line`, `job.c2p`). Restricciones: destino alineado a word, `source_shift` 0..15 px, sin clipping automático, `mask` es un plano de 1 bit compartido, `source` planar contiguo o intercalado.

`mod16`/`mod16u` (`blit_job.hpp:25`, `:30`) convierten un módulo calculado (`row_bytes·planes − words·2`) a `s16` **comprobando el rango** — evitan el truncado silencioso de un `static_cast` a ciegas.

## Extensión por etiquetas — `blit_queue.hpp`

`BlitQueue<N, Executor>` (`blit_queue.hpp:274`) encola **intenciones** de blit (`fill`, `stamp`, `masked_stamp`, `all`) que un ejecutor convierte en trabajo canónico: `blit_job_from(op)` (`blit_queue.hpp:62`) da el `BlitJob`, y `blitter_job_from(j)` (`blit_queue.hpp:115`) los registros (`BlitterJob`). La intención no inventa un «juego de registros» paralelo: reusa el trabajo que el `FramePlan` ya describe.

> Referencia de minterms y modos (constantes en `blitter_state.hpp`): `kBlitterMintermAOrB = $FC`, `kBlitterMintermCookieCut = $CA`, `kBlitterMintermCopyA = $F0` (`blitter_state.hpp:32`). Detalle de hardware en `docs/reference/amiga/techniques/`.

Volver al [índice de `graphics/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
