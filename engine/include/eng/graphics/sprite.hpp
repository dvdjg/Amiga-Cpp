#pragma once

/// \file sprite.hpp
/// Plantillas portables para sprites hardware (la nueva estructura del engine).
///
/// Construye sobre `SpriteManager` (el emitter final que escribe `SPRxPT/POS/CTL`) y
/// sobre `Visual`/`SpriteIntent` (`raster_intent.hpp`). La plantilla describe, sin
/// escribir registros, cómo UNA imagen fuente se traduce a:
///
/// - **segmentos reutilizables** (reuso vertical: el mismo canal dibuja cosas distintas
///   de arriba abajo, y multiplexado: "chasing the raster" de la demoscene);
/// - **cambios de paleta por franja** (color multiplexing: sprite que absorbe el color
///   de fondo por scanline);
/// - **attached pairs** para 15 colores.
///
/// Invariantes heredadas de la auditoría de amiga-bootcamp:
///   - la DATA vive en Chip RAM (DMA); las plantillas solo la referencian por `Span`;
///   - los pares comparten sus 3 `COLORxx` (cambiar el color de un canal afecta a su
///     par: "The Color Bleed"); los switches lo respetan por construcción.
///
/// Los tipos son puros (sin hardware, sin STL): host-testables. El `SpriteAllocator`
/// (futuro) procesará las plantillas y decidirá canales/multiplexado.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::graphics {

/// Franja reutilizable de una imagen de sprite fuente.
///
/// Un sprite hardware de 16/32 px puede recortarse en franjas verticales que el
/// `SpriteAllocator` serve en líneas distintas del mismo canal: `data_offset` es el
/// desplazamiento en words desde el inicio del bitmap fuente, y `height` la longitud.
struct SpriteSegment {
    u16 data_offset = 0;  // words desde el inicio del bitmap fuente (incluye DAT/DATB)
    u16 height = 0;       // lineas de esta franja
    u16 y_in_bitmap = 0;  // linea inicial dentro de la imagen fuente (referencia visual)
};

/// Cambio de paleta asociado a una franja (color multiplexing por scanline).
///
/// En `line` el Copper programa `COLORxx` (`first/count` de `colors`) para la franja.
/// Es el equivalente portable del "sprite absorbe el color de fondo" de la demoscene.
struct SpritePaletteSwitch {
    u16 line = 0;                  // linea raster de disparo
    const u16* colors = nullptr;   // los valores COLORxx a programar
    u8  first = 0;                 // primer registro COLORxx
    u8  count = 0;                 // cuantos
};

/// Plantilla portable de un sprite hardware.
///
/// `MaxSegments` y `MaxPaletteSwitches` son parametros de plantilla para mantener el
/// array fijo sin heap (regla del engine). `bitmap` es la imagen fuente cocinada
/// (DAT/DATB intercalados), una sola por canon; los segmentos trocean esa imagen.
template <u8 MaxSegments, u8 MaxPaletteSwitches>
struct SpriteTemplate {
    Span<const u16> bitmap {};                     // imagen fuente (Chip RAM)
    SpriteSegment segments[MaxSegments] {};
    SpritePaletteSwitch switches[MaxPaletteSwitches] {};
    u8 segment_count = 0;
    u8 switch_count = 0;
    u8 width_words = 1;    // 1 = 16 px, 2 = 32 px (SPRxCTL doble ancho)
    bool attach = false;   // true = attached al canal anterior (15 colores)

    /// Añade un segmento al final si cabe.
    bool add_segment(SpriteSegment seg) {
        if (segment_count >= MaxSegments) return false;
        segments[segment_count++] = seg;
        return true;
    }

    /// Añade un cambio de paleta al final si cabe.
    bool add_switch(SpritePaletteSwitch sw) {
        if (switch_count >= MaxPaletteSwitches) return false;
        switches[switch_count++] = sw;
        return true;
    }
};

} // namespace eng::graphics
