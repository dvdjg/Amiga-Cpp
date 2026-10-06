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

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/util/array.hpp>
#include <eng/graphics/raster_intent.hpp>

namespace eng::graphics {

/// Franja reutilizable de una imagen de sprite fuente.
///
/// Un sprite hardware de 16/32 px puede recortarse en franjas verticales que el
/// `SpriteAllocator` serve en líneas distintas del mismo canal: `data_offset` es el
/// desplazamiento en words desde el inicio del bitmap fuente, y `height` la longitud.
struct HwSpriteSegment {
    u16 data_offset = 0;  // words desde el inicio del bitmap fuente (incluye DAT/DATB)
    u16 height = 0;       // lineas de esta franja
    u16 y_in_bitmap = 0;  // linea inicial dentro de la imagen fuente (referencia visual)
};

/// Cambio de paleta asociado a una franja (color multiplexing por scanline).
///
/// En `line` el Copper programa `COLORxx` (`first/count` de `colors`) para la franja.
/// Es el equivalente portable del "sprite absorbe el color de fondo" de la demoscene.
struct HwSpritePaletteSwitch {
    u16 line = 0;                  // linea raster de disparo
    const u16* colors = nullptr;   // los valores COLORxx a programar
    u8  first = 0;                 // primer registro COLORxx
    u8  count = 0;                 // cuantos
};

/// **Vista de una `HwSpriteTemplate` sin capacidad fija** (camino de actores): un
/// `ActorDesc` puede referenciar una plantilla cocinada (`bitmap` en Chip + segmentos +
/// switches de paleta) sin conocer el `MaxSegments`/`MaxPaletteSwitches` con que se
/// construyó. `empty()` = sin plantilla (el actor se sirve como sprite normal).
struct HwSpriteTemplateView {
    Span<const u16> bitmap {};                        ///< imagen fuente (Chip; DAT/DATB)
    Span<const HwSpriteSegment> segments {};          ///< franjas (offsets en words al bitmap)
    Span<const HwSpritePaletteSwitch> switches {};    ///< cambios de COLORxx por franja
    u8 width_words = 1;                               ///< 1 = 16 px, 2 = 32 px
    bool attach = false;                              ///< par *attached* (15 colores)

    /// ¿Sin plantilla? (sin segmentos o sin bitmap): el actor se sirve como sprite normal.
    [[nodiscard]] constexpr bool empty() const noexcept {
        return segments.empty() || bitmap.empty();
    }
};

/// Plantilla portable de un sprite hardware.
///
/// `MaxSegments` y `MaxPaletteSwitches` son parametros de plantilla para mantener el
/// array fijo sin heap (regla del engine). `bitmap` es la imagen fuente cocinada
/// (DAT/DATB intercalados), una sola por canon; los segmentos trocean esa imagen.
template <u8 MaxSegments, u8 MaxPaletteSwitches>
struct HwSpriteTemplate {
    Span<const u16> bitmap {};                     // imagen fuente (Chip RAM)
    eng::util::Array<HwSpriteSegment, MaxSegments> segments {};
    eng::util::Array<HwSpritePaletteSwitch, MaxPaletteSwitches> switches {};
    u8 segment_count = 0;
    u8 switch_count = 0;
    u8 width_words = 1;    // 1 = 16 px, 2 = 32 px (SPRxCTL doble ancho)
    bool attach = false;   // true = attached al canal anterior (15 colores)

    /// Añade un segmento al final si cabe.
    bool add_segment(HwSpriteSegment seg) {
        if (segment_count >= MaxSegments) return false;
        segments[segment_count++] = seg;
        return true;
    }

    /// Añade un cambio de paleta al final si cabe.
    bool add_switch(HwSpritePaletteSwitch sw) {
        if (switch_count >= MaxPaletteSwitches) return false;
        switches[switch_count++] = sw;
        return true;
    }

    /// **Vista sin capacidad** de esta plantilla (para el camino de actores): el actor
    /// referencia la plantilla cocinada sin arrastrar sus arrays de tamaño fijo.
    [[nodiscard]] constexpr HwSpriteTemplateView view() const noexcept {
        return HwSpriteTemplateView {
            bitmap,
            Span<const HwSpriteSegment> {segments.data(), segment_count},
            Span<const HwSpritePaletteSwitch> {switches.data(), switch_count},
            width_words,
            attach,
        };
    }
};

/// Sprite ya repartido a un canal hardware: la salida del compositor hacia el emisor de
/// sprites (`SpriteManager`), que escribe `SPRxPOS/CTL/PT`. El compositor no escribe
/// registros; solo publica este contrato.
///
/// `data` apunta a la DATA del sprite en **Chip RAM** con el banco en el tipo
/// (`ChipView<SpriteTag>`, la lee el DMA de sprites: pasar Fast/Slow o una vista sin banco
/// **no compila**). La prioridad frente a los playfields no es por sprite (es un registro
/// global, `BPLCON2`): viaja aquí para que el emisor la aplique una vez por frame.
struct HwSpritePlacement {
    u8 channel = 0;        ///< canal hardware 0..7
    u8 priority = 0;       ///< prioridad frente a los playfields (0..3, `BPLCON2`)
    u16 hpos = 0;          ///< X en píxeles
    u16 vstart = 0;        ///< primera línea raster (escala del compositor)
    u16 height = 0;        ///< líneas (1..128)
    u8 width_words = 1;    ///< 1 = 16 px, 2 = 32 px (doble ancho/attach)
    bool attach = false;   ///< attached al canal anterior (15 colores)
    ChipView<SpriteTag> data {}; ///< DATA del sprite (Chip; `height*2*width_words` words)
};

/// **Cambio de paleta por franja de una plantilla**, en el vocabulario del switch:
/// `colors[k]` se escribe en `COLOR[first+k]` (**0-based**, como
/// `HwSpritePaletteSwitch::colors`) y `line` va en la escala del llamador (absoluta para el
/// camino de actores). No se usa `CopperIntent::PaletteLine` para esto porque su contrato
/// indexa los colores de forma **absoluta** (`colors[first+i]`, ver `Scheduler::emit_palette`),
/// incompatible con el switch 0-based de la plantilla.
struct SpritePaletteEvent {
    u16 line = 0;              ///< línea (escala del llamador)
    const u16* colors = nullptr; ///< valores a programar (0-based)
    u8 first = 0;              ///< primer registro COLORxx
    u8 count = 0;              ///< cuántos
};

/// Salida de proyectar una `HwSpriteTemplate` al vocabulario de intenciones.
struct SpriteIntentSet {
    SpriteIntent* intents = nullptr;
    u8 intent_capacity = 0;
    u8 intent_count = 0;
    CopperIntent* copper = nullptr;
    u8 copper_capacity = 0;
    u8 copper_count = 0;
    SpritePaletteEvent* palette = nullptr;
    u8 palette_capacity = 0;
    u8 palette_count = 0;
    bool overflow = false; ///< true = alguna intención no cupo en su buffer
};

/// Proyecta una plantilla al vocabulario de intenciones del frame, **sin escribir
/// registros**:
///
///   - una `SpriteIntent` por franja, con el tramo que le toca tras aplicar el gap de
///     1 línea que exige el DMA (así el `SpriteAllocator` multiplexa el canal);
///   - una `CopperIntent::SpriteRearm` por cada franja a partir de la segunda, para
///     reapuntar el canal a la DATA de esa franja en su primera línea;
///   - un `SpritePaletteEvent` por cada `HwSpritePaletteSwitch` que caiga dentro
///     del tramo del sprite (0-based, como el switch).
///
/// `base_y` es la primera línea del sprite (la escala del llamador): las franjas empiezan
/// en `base_y` y las líneas de los switches son **relativas a `base_y`**, de modo que la
/// misma plantilla sirve para un objeto móvil (el compositor de actores pasa su `top`).
///
/// `chain_id != 0` marca las franjas como **cadena vertical** (`SpriteIntent::chain_*`):
/// todas deben servirse por el MISMO canal (el `SpriteAllocator` lo reserva para el rango
/// completo). Con `chain_id == 0` las franjas quedan sueltas (uso del driver directo).
/// El `first` de los switches es el registro COLOR: los sprites usan `COLOR16..31`.
///
/// Si `out.copper`/`out.palette` son nulos, esas intenciones se omiten en silencio (no
/// cuentan como desbordamiento): el compositor de actores materializa la paleta anclada al
/// actor en la escala del sprite.
inline void sprite_template_view_to_intents(const HwSpriteTemplateView& tpl, u8 channel,
					    u16 base_y, u16 hpos, u8 priority,
					    SpriteIntentSet& out, u8 chain_id = 0u) {
    auto emit_copper = [&out](const CopperIntent& c) {
        if (out.copper == nullptr) {
            return; // el llamador no recoge Copper (la paleta la emite el compositor)
        }
        if (out.copper_count < out.copper_capacity) {
            out.copper[out.copper_count++] = c;
        } else {
            out.overflow = true;
        }
    };
    const u8 seg_count =
        static_cast<u8>(tpl.segments.size() > 255u ? 255u : tpl.segments.size());
    u16 line = base_y;
    for (u8 i = 0; i < seg_count; ++i) {
        const HwSpriteSegment& seg = tpl.segments[i];
        if (out.intents != nullptr && out.intent_count < out.intent_capacity) {
            SpriteIntent it {};
            it.channel = channel;
            it.top = line;
            it.bottom = static_cast<u16>(line + seg.height);
            it.hpos = hpos;
            it.width_words = tpl.width_words;
            it.attach = tpl.attach;
            it.priority = priority;
            if (chain_id != 0u) {
                it.chain_id = chain_id;
                it.chain_index = i;
                it.chain_span = seg_count;
            }
            out.intents[out.intent_count++] = it;
        } else {
            out.overflow = true;
        }
        // La primera franja se carga con la DATA inicial del canal; las siguientes
        // necesitan rearme (SPRxPT) al empezar su tramo.
        if (i > 0u) {
            CopperIntent c {};
            c.kind = CopperIntentKind::SpriteRearm;
            c.top = line;
            c.bottom = line;
            c.sprite_channel = channel;
            c.sprite_ptr = tpl.bitmap.data() + seg.data_offset;
            emit_copper(c);
        }
        line = static_cast<u16>(line + seg.height + 1u); // +1: gap requerido por el DMA
    }
    const u16 end_line = line;
    const u8 sw_count =
        static_cast<u8>(tpl.switches.size() > 255u ? 255u : tpl.switches.size());
    for (u8 s = 0; s < sw_count; ++s) {
        const HwSpritePaletteSwitch& sw = tpl.switches[s];
        if (sw.line >= static_cast<u16>(end_line - base_y)) {
            continue; // fuera del tramo del sprite: no se proyecta
        }
        if (out.palette == nullptr) {
            continue; // el llamador no recoge la paleta (la emite el compositor)
        }
        if (out.palette_count < out.palette_capacity) {
            SpritePaletteEvent& ev = out.palette[out.palette_count++];
            ev.line = static_cast<u16>(base_y + sw.line);
            ev.colors = sw.colors;
            ev.first = sw.first;
            ev.count = sw.count;
        } else {
            out.overflow = true;
        }
    }
}

/// Azúcar: proyecta una `HwSpriteTemplate` de capacidad fija a través de su vista.
template <u8 MaxSegments, u8 MaxPaletteSwitches>
inline void sprite_template_to_intents(const HwSpriteTemplate<MaxSegments, MaxPaletteSwitches>& tpl,
				       u8 channel, u16 base_y, u16 hpos, u8 priority,
				       SpriteIntentSet& out, u8 chain_id = 0u) {
    sprite_template_view_to_intents(tpl.view(), channel, base_y, hpos, priority, out, chain_id);
}

} // namespace eng::graphics
