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
| `NewBitmap` / `DeleteBitmap` / `BitmapMakeDisplayable` | Bitmap planar generico reutilizable | **MAPEAR** (ya existe `eng::gfx::Bitmap`) | `eng/graphics/bitmap.hpp` |
| `BitmapSetPointers` / `InitSharedBitmap` | Layout interleave / compartir planos | **MAPEAR/absorber** en el bitmap portable | `eng/graphics/bitmap.hpp` |
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
2. **`c2p_1x1_4` a `support/c2p_1x1_4.s` + wrapper `eng::graphics::c2p` freestanding.**
   Es la pieza que permite meter píxeles de efectos CPU (chunky) a los bitplanes, que
   era el vacío claro de `PixmapT`.
3. **`Bitmap` portable** (`eng/graphics/bitmap.hpp`): YA EXISTE (`eng::gfx::Bitmap`), con
   `BitmapConfig` parametrico (width/height/planes/layout/row_bytes/alignment/domain/
   frontbase_offset/guard_bytes) y `byte_offset`/`bytes()`. Lo usan `Playfield` y
   `XLimitedPlayfield`. No hay que crearlo; solo reutilizarlo donde haga falta.
4. `math2d` (del `gfx.h`) y los `Effect` de paleta quedan para Oleada 3 (son puros y
   host-testables; no bloquean display base).

## 3-bis. Estado actual (2026-09)

| Pieza | Estado | Evidencia |
|---|---|---|
| `CopWaitSafe` / `wait_line_pal` | HECHO (utilidad), NO para split corkscrew | `ListBuilder::wait_line_pal(u16)` + `Scheduler::wait_line_safe(u16)` existen como port de `CopWaitSafe` de libgfx. PERO no resuelven el split vertical del corkscrew: la limitación es del comparador de 8 bits SIN bit V8 (verificado en WinUAE-DBG coppercomp), no del overflow. El doble-WAIT `0xffdf/0xfffe` tampoco compara V8 en el emulador → produce recortes incorrectos. Se revirtió su uso en `XlimitedDisplayComposer`; la demo 202 sigue con viewport 208 (ver AMIGA_8WAY_SCROLLING.md §12). La utilidad queda para casos donde el WAIT a línea >= 256 no necesite precisión. |
| `c2p_1x1_4` (C++) | HECHO + demo + asm verificado | Port FIEL del merge/butterfly de Kalms en `eng/graphics/c2p.hpp`. Cargas/escrituras `u32`/`u16` NATIVAS con `#if __m68k__` (byte-swap solo en host), por lo que g++ -O1 emite `move.l (a0)` + `move.w d0,(an)` (verificado con `-S`); no byte-a-byte. Demo `061_c2p_chunky_4bpl`: rampa de grises `i·0x111`, 15 tonos, 100% en rampa. |
| `c2p_1x1_4.asm` (Kalms) | ASM PORTADO | `support/c2p_1x1_4.s` conserva la rutina original (GAS). La version C++ es ya equivalente en rendimiento (mismo merge, cargas nativas); el asm queda como referencia de hot path / documentacion del desintercalado. |
| `prototypes/c2p/*.py` | NO importar | Son scripts Python de la demoscene que VISUALIZAN el bit-shuffle (colorean bits ANSI) para DERIVAR el asm, no para generarlo. Las "variantes" (1x1/2x1, 4/8bpl/ham6, CPU/blitter/sprites/mangled) son cada layout chunky→planar y cada coprocesador. No aportan como tool del engine; el porqué de las máscaras ya está documentado en `pixel_conversion.md` y en `c2p.hpp`. |
| `Bitmap` portable | HECHO (ya existia) | `eng/graphics/bitmap.hpp` (`eng::gfx::Bitmap`) YA era el Bitmap portable parametrico: `BitmapConfig` (width/height/planes/layout/row_bytes/alignment/domain/frontbase_offset/guard_bytes), `byte_offset`, `bytes()`, `blitter_accessible()`. Lo usan `Playfield` (`playfield.hpp`) y `XLimitedPlayfield` (`xlimited.hpp`). No habia que crearlo; correccion del inventario (regla: buscar antes de implementar). |

Nota sobre el build: `tools/build/build-demo.sh` ahora ensambla TODOS los `*.s` de
`support/` (antes solo `gcc8_a_support.s`), habilitando la incorporacion de mas asm del
repo (p61, pt, ahx...) sin tocar el script cada vez.

## 4. Validacion

Cada pieza portada sigue el pipeline: inventario -> portado C++23 -> test host o demo
con `build -> run -> analyze` -> cevidence -> update del coverage-index y este doc.

La regla de confirmacion de la ingesta: si ya existe equivalente en `eng::`, se mapea
y no se duplica. `libgfx/CopListT` es el caso canonico: su poder expresivo queda
cubierto por `Scheduler + CopperIntent`, con la ventaja de que el scheduler arbitra
presupuesto (Timeline) y el juego no ve registros (regla de oro del engine).