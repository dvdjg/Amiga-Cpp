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

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/blitter_state.hpp>

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
    u8  frame_count = 1;        // frames en la hoja (1 = imagen suelta)
    u32 frame_stride = 0;       // bytes entre frames (0 = denso/una sola imagen)
    u16 offset_x = 0;           // shift de blit (X no alineada a 16 px)
    u16 palette_base = 16;      // COLORxx base (sprites usan COLOR16+)
};

/// Qué registro/grupo de registros cambia una intención de Copper.
///
/// El vocabulario cubre las familias de efecto por raster que usan las demos. `BlitterJob`
/// cubre el lanzamiento de un blit (`BLTCON*`/punteros/módulos/`BLTSIZE`). Para "barrer todo
/// lo que se puede hacer con el Copper" faltan explícitamente:
///
/// - `Wait`/`Skip` crudos y `MoveSequence` opacos: hoy se emiten con
///   `Scheduler::wait_position`/`move` directamente; un kind portable capturaría efectos
///   que no son "paleta" ni "layout" (p. ej. cambiar `DMACON` en una línea).
/// - `SpriteAttach` como intención explícita (hoy viaja en `SpriteIntent::attach`).
/// - `BitplaneModulo` por franja (`BPL1MOD`/`BPL2MOD`), base de los trucos de
///   `modulo-tricks.md` (fetch discontinuo, líneas de repetición).
/// - `CopperJump`/`COP2LC` (sub-listas con `COPJMP2`), para tablas de efectos reusables.
///
/// El rearmado **horizontal** de sprites sí está modelado, pero NO como kind: ocurre
/// varias veces en la MISMA línea y es carrera contra el haz, así que no encaja en la
/// semántica "un cambio por franja en `top`". Ver `SpriteHorizontalRearm`.
enum class CopperIntentKind : u8 {
    PaletteLine,    // COLORxx a partir de la linea `top`
    PaletteSpan,    // COLORxx a mitad de linea (hpos): "copper bar"
    ShiftLines,     // offset-X por linea (scroll/ondas): BPLxPT/BPLCON1
    BitplaneSplit,  // reapuntar planos a media pantalla (HUD, bandas)
    SpriteRearm,    // reapuntar SPRxPT/POS/CTL (multiplexado vertical)
    Priority,       // cambiar prioridad sprite/playfield (BPLCON2)
    BlitterJob,     // programar el Blitter y escribir BLTSIZE (blit lanzado por el Copper)
};

/// **Trabajo de Blitter lanzado por el Copper** (Técnica A: Copper → Blitter).
///
/// El Copper programa los registros del Blitter en la linea `CopperIntent::top` y escribe
/// `BLTSIZE` **al final** (arranca el blit). Sirve para un blit **sincronizado al haz**
/// (reparar el borde de un scroll, copiar la columna nueva, HUD, cola de blits) sin coste de
/// CPU por el arranque.
///
/// El Blitter es **único**: este job se serializa con los blits de CPU. El `Scheduler`
/// solo lo emite si la linea cae en la **ventana segura** declarada con
/// `Scheduler::set_blitter_window` (p. ej. el borde inferior, fuera del fetch de bitplanes y
/// de los blits de CPU que hace `present`). Ver
/// `docs/reference/amiga/techniques/blitter-cpu-interleaving.md` y
/// `docs/guides/roadmap/ROADMAP_BLITTER_COPPER.md`.
///
/// Los punteros nulos se omiten (no se escriben). Convención de uso: para `D = A` (copia)
/// basta `bltcon0 = USEA|USED|minterm $F0`; para `D = A | D` (BOB OR) `USEA|USEB|USED|$FC`
/// con B = D = destino.
struct BlitterJob {
    u16 bltcon0 = 0;
    u16 bltcon1 = 0;
    u16 bltafwm = 0xffff;
    u16 bltalwm = 0xffff;
    s16 bltamod = 0;
    s16 bltbmod = 0;
    s16 bltcmod = 0;
    s16 bltdmod = 0;
    const void* bltapt = nullptr;
    const void* bltbpt = nullptr;
    const void* bltcpt = nullptr;
    void* bltdpt = nullptr;
    u16 bltsize = 0; ///< se escribe el ULTIMO: dispara el blit
};

/// **Ventana segura** (rango de líneas, inclusivo) para lanzar un `BlitterJob` desde el
/// Copper. Debe excluir el área visible (fetch de bitplanes) y el momento en que la CPU
/// lanza blits (`present`); lo típico es el borde inferior o el VBlank.
struct BlitterWindow {
    u16 first = 0;
    u16 last = 0;
    [[nodiscard]] constexpr bool contains(u16 line) const noexcept {
        return line >= first && line <= last;
    }
};

/// **Ventana segura automática** para un blit de Copper que debe caer **después** de los blits
/// de CPU: su comienzo es el mayor entre `border_line` (p. ej. el borde inferior) y el fin
/// **estimado** del trabajo de CPU (`cpu_start_line + blitter_lines(cpu_blit_words)`). Así el
/// llamador no depende de una línea cableada: el `BLTSIZE` del Copper no puede abortar un blit
/// de CPU en curso (el Blitter es único). Ver `blitter-memcpy.md` §Concurrencia.
[[nodiscard]] constexpr BlitterWindow safe_blitter_window(u16 cpu_blit_words, u16 cpu_start_line,
                                                          u16 border_line, u16 last_line) noexcept {
    const u32 cpu_end = static_cast<u32>(cpu_start_line) + blitter_lines(cpu_blit_words);
    u16 start = (cpu_end > border_line) ? static_cast<u16>(cpu_end) : border_line;
    if (start > last_line) {
        start = last_line;
    }
    return BlitterWindow { start, last_line };
}

/// **Rearmado horizontal de un canal de sprite** (multiplexado horizontal por línea).
///
/// A diferencia del multiplexado vertical (`CopperIntent::SpriteRearm`, que reapunta el
/// canal en OTRA línea), éste redibuja el MISMO canal más a la derecha en la MISMA línea:
/// el Copper reposiciona (`SPRxPOS`/`SPRxCTL`) y recarga la imagen (`SPRxDATA`/`SPRxDATB`)
/// varias veces mientras el haz barre. Es la base de los fondos continuos tipo
/// **Risky Woods** (2 canales, patrón de 64 px repetitivo a 15 colores) y del **Free Form
/// Sprite Layer** (los 8 canales, fondo sin patrón). Ver
/// `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`.
///
/// **No toca `SPRxPT`**: el puntero apunta a una estructura POS/CTL+DATA; reasignarlo
/// relanzaría la secuencia DMA. En modo manual se escriben los registros directos.
///
/// Modelo de coste: es **carrera contra el haz**, no presupuesto por frame. Hacen falta
/// **≥24 px** entre usos del mismo canal para que el Copper llegue a escribir los
/// registros a tiempo (medido por la fuente). El `Timeline` no lo presupuesta.
struct SpriteHorizontalRearm {
    u8  channel = 0;              // canal 0..7
    u16 hpos = 0;                 // posición horizontal (low-res px; se codifica /2)
    u16 vstart = 0;               // VSTART del tramo (línea de la primera fila)
    u16 vstop = 0;                // VSTOP (línea DESPUÉS de la última fila)
    u16 data_high = 0;            // SPRxDATA (primera palabra de la fila)
    u16 data_low = 0;             // SPRxDATB (segunda palabra de la fila)
    bool attach = false;          // bit de attach (par de canales a 15 colores)
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
    eng::PaletteWords colors {};  // paleta de dominio (PaletteLine/PaletteSpan)
    // `first` es el REGISTRO COLOR de arranque (0 = COLOR00) y a la vez el indice
    // dentro de `colors`: el scheduler escribe COLOR[first+i] = colors[first+i].
    // Para cambiar COLOR00 a un color suelto, pasa una vista de 1 color en `colors`
    // (`PaletteWords{&color, 1}`) con `first = 0`.
    u8  first = 0;
    u8  count = 0;
    s16 shift_x = 0;              // ShiftLines
    eng::ChipPlaneView bitplanes {};  // BitplaneSplit (base del primer plano, Chip)
    u8  sprite_channel = 0;       // SpriteRearm
    const u16* sprite_ptr = nullptr; // SpriteRearm (nueva DATA del canal)
    const BlitterJob* blitter_job = nullptr; // BlitterJob (registros a programar)
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
    /// Prioridad del sprite FRENTE A LOS PLAYFIELDS (0..3, modo `BPLCON2`): un sprite
    /// puede quedar delante o detrás de PF1/PF2. No es el `z` de los BOBs (que ordena
    /// objetos dentro de un mismo playfield).
    u8  priority = 0;
    /// Tira horizontal: grupo de canales CONTIGUOS que forman juntos un objeto más
    /// ancho que un sprite (fondos de sprites tipo Risky Woods / Jim Power). `strip_id`
    /// 0 = sprite suelto; los miembros de una tira comparten `strip_id`, declaran el
    /// MISMO `strip_span` (canales que ocupa) y el mismo rango vertical, y llegan
    /// ordenados con el líder (`strip_index == 0`, el tramo de la izquierda) PRIMERO y
    /// los demás a continuación. El allocator reserva una corrida de canales contiguos.
    u8  strip_id = 0;
    u8  strip_index = 0;
    u8  strip_span = 1;
};

} // namespace eng::graphics

namespace eng::graphics {

/// Contrato de un efecto: avanza su estado temporal y aporta intenciones a un plan.
///
/// Un efecto no escribe hardware: `apply_into` solo registra intenciones
/// (`CopperIntent`, `SpriteIntent`, parches de paleta). El tipo `Plan` queda generico
/// (normalmente `FramePlan`, definido en `frame_plan.hpp`) y `Tick` es normalmente
/// `u16` (el indice de frame), para no acoplar este header a tipos concretos. Es el
/// punto por el que `PaletteCycleEffect` y los futuros efectos pasan a ser ciudadanos
/// de primer orden del engine.
template <typename E, typename Plan, typename Tick = u16>
concept Effect = requires(E e, Plan& plan, Tick tick) {
    e.update(tick);       // avanza el estado temporal con el tick (indice de frame)
    e.apply_into(plan);   // aporta intenciones (CopperIntent/SpriteIntent/paleta)
};

} // namespace eng::graphics
