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

#include <eng/core/domains.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/graphics/raster_intent.hpp>

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

/// Sprite ya repartido a un canal hardware: la salida del compositor hacia el emisor de
/// sprites (`SpriteManager`), que escribe `SPRxPOS/CTL/PT`. El compositor no escribe
/// registros; solo publica este contrato.
///
/// `data` apunta a la DATA del sprite en Chip RAM (`height * width_words * 2` words, con
/// DAT/DATB intercalados por línea). La prioridad frente a los playfields no es por
/// sprite (es un registro global, `BPLCON2`): viaja aquí para que el emisor la aplique
/// una vez por frame.
struct SpritePlacement {
    u8 channel = 0;        ///< canal hardware 0..7
    u8 priority = 0;       ///< prioridad frente a los playfields (0..3, `BPLCON2`)
    u16 hpos = 0;          ///< X en píxeles
    u16 vstart = 0;        ///< primera línea raster (escala del compositor)
    u16 height = 0;        ///< líneas (1..128)
    u8 width_words = 1;    ///< 1 = 16 px, 2 = 32 px (doble ancho/attach)
    bool attach = false;   ///< attached al canal anterior (15 colores)
    const u16* data = nullptr; ///< DATA del sprite (Chip RAM)
};

/// Salida de proyectar una `SpriteTemplate` al vocabulario de intenciones.
struct SpriteIntentSet {
    SpriteIntent* intents = nullptr;
    u8 intent_capacity = 0;
    u8 intent_count = 0;
    CopperIntent* copper = nullptr;
    u8 copper_capacity = 0;
    u8 copper_count = 0;
    bool overflow = false; ///< true = alguna intención no cupo en su buffer
};

/// Proyecta una plantilla al vocabulario de intenciones del frame, **sin escribir
/// registros**:
///
///   - una `SpriteIntent` por franja, con el tramo que le toca tras aplicar el gap de
///     1 línea que exige el DMA (así el `SpriteAllocator` multiplexa el canal);
///   - una `CopperIntent::SpriteRearm` por cada franja a partir de la segunda, para
///     reapuntar el canal a la DATA de esa franja en su primera línea;
///   - una `CopperIntent::PaletteLine` por cada `SpritePaletteSwitch` que caiga dentro
///     del tramo ocupado por el sprite.
///
/// `base_y` y `SpritePaletteSwitch::line` van en la MISMA escala de línea raster que usa
/// `SpriteManager::emit_template_into` (es una proyección, no un cambio de escala). El
/// `first` de los switches es el registro COLOR: los sprites usan `COLOR16..31`. El
/// compositor (`copper::Plan`) ordena después las intenciones por línea.
template <u8 MaxSegments, u8 MaxPaletteSwitches>
inline void sprite_template_to_intents(const SpriteTemplate<MaxSegments, MaxPaletteSwitches>& tpl,
				       u8 channel, u16 base_y, u16 hpos, u8 priority,
				       SpriteIntentSet& out) {
    u16 line = base_y;
    for (u8 i = 0; i < tpl.segment_count; ++i) {
        const SpriteSegment& seg = tpl.segments[i];
        if (out.intents != nullptr && out.intent_count < out.intent_capacity) {
            SpriteIntent it {};
            it.channel = channel;
            it.top = line;
            it.bottom = static_cast<u16>(line + seg.height);
            it.hpos = hpos;
            it.width_words = tpl.width_words;
            it.attach = tpl.attach;
            it.priority = priority;
            out.intents[out.intent_count++] = it;
        } else {
            out.overflow = true;
        }
        // La primera franja se carga con la DATA inicial del canal; las siguientes
        // necesitan rearme (SPRxPT) al empezar su tramo.
        if (i > 0u) {
            if (out.copper != nullptr && out.copper_count < out.copper_capacity) {
                CopperIntent c {};
                c.kind = CopperIntentKind::SpriteRearm;
                c.top = line;
                c.bottom = line;
                c.sprite_channel = channel;
                c.sprite_ptr = tpl.bitmap.data() + seg.data_offset;
                out.copper[out.copper_count++] = c;
            } else {
                out.overflow = true;
            }
        }
        line = static_cast<u16>(line + seg.height + 1u); // +1: gap requerido por el DMA
    }
    const u16 end_line = line;
    for (u8 s = 0; s < tpl.switch_count; ++s) {
        const SpritePaletteSwitch& sw = tpl.switches[s];
        if (sw.line < base_y || sw.line >= end_line) {
            continue; // fuera del tramo del sprite: no se proyecta
        }
        if (out.copper != nullptr && out.copper_count < out.copper_capacity) {
            CopperIntent c {};
            c.kind = CopperIntentKind::PaletteLine;
            c.top = sw.line;
            c.bottom = sw.line;
            c.colors = eng::PaletteWords {sw.colors, sw.count};
            c.first = sw.first;
            c.count = sw.count;
            out.copper[out.copper_count++] = c;
        } else {
            out.overflow = true;
        }
    }
}

} // namespace eng::graphics
