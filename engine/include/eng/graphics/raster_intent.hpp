#pragma once

/// \file raster_intent.hpp
/// Vocabulario portable de intenciones de display (la nueva estructura del engine).
///
/// Este header es el punto de union de la parte grafica descrita en
/// `docs/engine/architecture/VISUAL_EFFECT_SPRITE_DESIGN.md` y `ENGINE_DESIGN.md`.
/// Define los tipos que capturan LO QUE la escena quiere mostrar/animar, sin decidir
/// COMO se materializa en hardware:
///
/// - `Visual`: el contenido dibujable de un objeto (BOB, sprite, tile, rect).
/// - `CopperIntent`: un cambio de registros en una franja vertical de lineas raster
///   (cambio de paleta, shift por linea, split de planos, rearmado de sprites...).
/// - `SpriteIntent`: la asignacion de un `Visual` de tipo sprite a un canal hardware.
/// - `Effect` (concept): un productor de intenciones con estado temporal por frame.
///
/// Ninguno de estos tipos escribe registros ni conoce DMA. Los schedulers
/// (`CopperScheduler`, `BlitterQueue`, `SpriteAllocator`) son los duenos unicos de los
/// coprocesadores y los compilan a `FramePlan`. Esta separacion es la regla de oro:
/// la logica de juego emite intenciones; el engine arbitra.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::graphics {

/// Cómo se materializa un `Visual`. El dato es el mismo; cambia el backend/allocator.
///
/// La transicion "sprite hardware que en el juego se cambia por un BOB equivalente sin
/// que se note" se resuelve con que el `Visual` NO cambia de identidad: solo cambia el
/// `kind` que el `SpriteAllocator`/`BlitterQueue` materializan este frame. Como sprite
/// y BOB comparten paleta y geometria, la transicion es imperceptible de forma natural.
enum class VisualKind : u8 {
    Bob,             // blitter cookie-cut con mascara
    HardwareSprite,  // 16 px (1 word) o 32 px (2 words), 1..128 lineas
    Tile,            // tile 16x16 de un tilemap
    FillRect,        // rectangulo de color plano
};

/// Descriptor portable de un objeto dibujable (retained, sin registros ni DMA).
///
/// `pixels`/`mask` son `Span` a memoria ya cocinada (Chip RAM si el backend es Amiga y
/// la consume DMA). El `Visual` no posee memoria; el `AssetRuntime` la gestiona.
struct Visual {
    VisualKind kind = VisualKind::Bob;
    Span<const u16> pixels {};  // data planar cocinada
    Span<const u16> mask {};    // 1 plano, opcional (cookie-cut)
    u16 w = 0;
    u16 h = 0;
    u8  bitplanes = 0;
    u16 offset_x = 0;           // shift de blit (X no alineada a 16 px)
    u16 palette_base = 16;      // COLORxx base (sprites usan COLOR16+)
};

/// Qué registro/grupo de registros cambia una intención de Copper.
enum class CopperIntentKind : u8 {
    PaletteLine,    // COLORxx a partir de la linea `top`
    PaletteSpan,    // COLORxx a mitad de linea (hpos): "copper bar"
    ShiftLines,     // offset-X por linea (scroll/ondas): BPLxPT/BPLCON1
    BitplaneSplit,  // reapuntar planos a media pantalla (HUD, bandas)
    SpriteRearm,    // reapuntar SPRxPT/POS/CTL (multiplexado vertical)
    Priority,       // cambiar prioridad sprite/playfield (BPLCON2)
};

/// Cambio portable sobre una franja vertical de lineas raster.
///
/// Esta es la abstraccion que unifica "BOB con cambio de paleta", "playfield con scroll
/// por linea" y "sprite con color por zona": todos son listas de `CopperIntent`
/// anotadas por franja. El `CopperScheduler` decide, con la `Timeline`, si caben en el
/// H-BLANK de cada linea y como se mezclan con la escena (nunca se escriben registros
/// desde aqui).
struct CopperIntent {
    CopperIntentKind kind = CopperIntentKind::PaletteLine;
    u16 top = 0;    // linea raster de inicio (inclusiva)
    u16 bottom = 0; // linea raster de fin (exclusiva), o igual a `top` si es puntual
    u16 hpos = 0;   // para PaletteSpan (posicion horizontal, unidades de WAIT)
    const u16* colors = nullptr;  // Palette* (los valores dependen del backend)
    u8  first = 0;
    u8  count = 0;
    s16 shift_x = 0;              // ShiftLines
    const u8* bitplanes = nullptr;// BitplaneSplit (base del primer plano)
    u8  sprite_channel = 0;       // SpriteRearm
    const u16* sprite_ptr = nullptr; // SpriteRearm (nueva DATA del canal)
};

/// Asignacion de un sprite hardware a un canal, para el `SpriteAllocator`.
///
/// Es la intencion que resume "este frame el `Visual` X se dibuja como sprite en el
/// canal N entre las lineas [top,bottom)". El allocator decide el canal final y
/// multiplexa. La data (DAT/DATB) sale del `Visual.pixels`.
struct SpriteIntent {
    u8  visual_index = 0;      // indice del `Visual` en la escena retenida
    u8  channel = 0;           // canal 0..7 propuesto (el allocator puede reasignar)
    u16 top = 0;
    u16 bottom = 0;
    u16 hpos = 0;
    u8  width_words = 1;       // 16 px o 32 px
    bool attach = false;       // attached al canal anterior (15 colores)
};

} // namespace eng::graphics

namespace eng::graphics {

/// Contrato de un efecto: avanza su estado temporal y aporta intenciones a un plan.
///
/// Un efecto no escribe hardware: `apply_into` solo registra intenciones
/// (`CopperIntent`, `SpriteIntent`, parches de paleta). El tipo `Plan` queda generico
/// (normalmente `FramePlan`, definido en `frame_plan.hpp`): aqui no dependemos de el
/// para mantener este header autocontenido y host-testable. Es el punto por el que
/// `PaletteCycleEffect` (ya existente) y los futuros efectos pasan a ser ciudadanos de
/// primer orden del engine.
template <typename E, typename Plan>
concept Effect = requires(E e, Plan& plan) {
    e.update();          // avanza el estado temporal del efecto (un tick)
    e.apply_into(plan);  // aporta intenciones (CopperIntent/SpriteIntent/paleta) al plan
};

} // namespace eng::graphics
