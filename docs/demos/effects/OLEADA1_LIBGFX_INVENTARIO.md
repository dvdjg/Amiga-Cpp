# Inventario y mapeo — Oleada 1, `libgfx` (display base)

Documento de trabajo para la Oleada 1 de `LIBRARIES-CPP23-IMPORT-ROADMAP.md` sobre la
**nueva estructura del engine** (`ENGINE_DESIGN.md` + `VISUAL_EFFECT_SPRITE_DESIGN.md`):
`CopperIntent`, `Visual`, `SpriteTemplate`, `Effect`, `CopperScheduler`.

## 1. Objetivo

Decidir, pieza a pieza de `demoscene-repo-orig/lib/libgfx` (y de sus headers públicos en
`include/`), qué se **mapea** a lo que ya existe en el engine, qué se **porta** (no existe
y aporta) y qué se **descarta**. Regla de oro: no duplicar; solo se porta lo que aporta.

Fuentes inventariadas:
`include/bitmap.h`, `include/copper.h`, `include/gfx.h`, `include/pixmap.h`,
`include/palette.h`, `include/sprite.h`, `include/colcolor.h`, `lib/libgfx/*.c` y
`lib/libgfx/c2p_1x1_4.asm`.

## 2. Decisión por pieza

| Origen | Qué hace | Decisión | Dónde en el engine |
|---|---|---|---|
| `CopListT` (`curr/length/overflow/finished/entry[]`) | Buffer mutable de instrucciones de copper | **MAPEAR** (el engine ya lo tiene mejorado) | `eng::copper::ListBuilder` + `Scheduler` |
| `CopWait` / `CopWaitMask` / `CopSkip` | WAIT/SKIP a una posicion de beam | **MAPEAR** | `Scheduler::wait_line` / `wait_position` (nuevo) / (`SKIP` pendiente) |
| `CopMove16` / `CopMove32` / `CopMove32` | MOVE a registro custom (word / long ptr) | **MAPEAR** | `Scheduler::move` / `ListBuilder::move_bitplane_pointer` |
| `CopSetColor` / `CopLoadColor` / `CopLoadColorArray` | Programar COLORxx | **MAPEAR** | `Scheduler::emit_palette` (ya cubre `first/count`) |
| `CopSetupMode` / `CopSetupDisplayWindow` / `CopSetupBitplaneFetch` | DIW/DDF/BPLCON0 | **MAPEAR** | `Scheduler::emit_planes_display` |
| `CopSetupBitplanes` / `CopUpdateBitplanes` | BPLxPT por bitmap | **MAPEAR** | `Scheduler::emit_planes_display` (punteros) |
| `CopSetupSprites` / `MakeSprite` / `EndSprite` | SPRxPT/POS/CTL | **MAPEAR** | `SpriteManager` + `SpriteTemplate` (nueva) |
| `CopWaitSafe` | Overflow de V>255 en PAL (lineas 256..311) | **PORTAR**: el engine aun no lo modela y es un bug real | `Scheduler::wait_line` (anadir el doble-WAIT) |
| `NewBitmap` / `DeleteBitmap` / `BitmapMakeDisplayable` | Bitmap planar generico reutilizable | **PORTAR** (API parametrica) | `eng::graphics::bitmap.hpp` (nuevo) |
| `BitmapSetPointers` / `InitSharedBitmap` | Layout interleave / compartir planos | **MAPEAR/absorber** en el bitmap portable | `eng::graphics::bitmap.hpp` |
| `c2p_1x1_4.asm` | Conversion chunky 4bpp -> planar | **PORTAR ASM** | `support/c2p_1x1_4.s` + wrapper `eng::graphics::c2p` |
| `PixmapT` + `PixmapScramble_4_1/_4_2` | Buffer chunky + reordenado | **MAPEAR** parcialmente | `eng::core::span` + helper de c2p |
| `NewPixmap`/`DeletePixmap` | Alloc chunky | **DESCARTAR** (se usa arena del engine) | — |
| `gfx.h` (`Point2D/Line2D/Box2D/Area2D`, `ClipArea`, `InsideArea`) | Geometria 2D + clip | **PORTAR** (puro, host-testable) | `eng::core::math2d` (Oleada 3) |
| `palette.h` (`ColorTab`, `HsvToRgb`, `ColorTransition`, `FadeBlack`) | Paletas / HSV / fades | **MAPEAR** a `Effect` | `eng::graphics::effects` (fade como `Effect`) |
| `Circle/CircleEdge/CpuLine/CpuEdge` | Primitivas raster CPU | **DESCARTAR** o Oleada 3 (depende de math2d/libblit) | — |

## 3. Decisiones concretas de la Oleada 1 (orden de trabajo)

1. **`CopWaitSafe` al `Scheduler`** (portar el manejo de overflow PAL). Es la pieza de
   bajo nivel que el engine no tiene y que rompe listas en PAL si falta. Se porta como
   mejora de `wait_line` (que ya usa `m_builder`), no como `CopListT`.
2. **`c2p_1x1_4`** a `support/c2p_1x1_4.s` + wrapper `eng::graphics::c2p` freestanding.
   Es la pieza que permite meter píxeles de efectos CPU (chunky) a los bitplanes, que
   era el vacío claro de `PixmapT`.
3. **`Bitmap` portable** (`eng/graphics/bitmap.hpp`): `Bitmap` parametrico
   (`width/height/depth/bytes_per_row/planes[]` via `MemorySystem`), con
   `BitmapMakeDisplayable`. Lo que hoy esta interno en `StaticEhbScene` pasaria a ser
   reutilizable de forma agnostica (regla de API parametrica).
4. `math2d` (del `gfx.h`) y los `Effect` de paleta quedan para Oleada 3 (son puros y
   host-testables; no bloquean display base).

## 4. Validacion

Cada pieza portada sigue el pipeline: inventario -> portado C++23 -> test host o demo
con `build -> run -> analyze` -> cevidence -> update del coverage-index y este doc.

La regla de confirmacion de la ingesta: si ya existe equivalente en `eng::`, se mapea
y no se duplica. `libgfx/CopListT` es el caso canonico: su poder expresivo queda
cubierto por `Scheduler + CopperIntent`, con la ventaja de que el scheduler arbitra
presupuesto (Timeline) y el juego no ve registros (regla de oro del engine).