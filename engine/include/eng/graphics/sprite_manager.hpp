#pragma once

/// \file sprite_manager.hpp
/// Gestión de sprites hardware del Amiga OCS a nivel de escena.
///
/// El chipset dispone de 8 sprites DMA. Cada sprite:
///   - es un bitmap de 1-2 words de ancho (16/32 px) y hasta 128 líneas;
///   - su DATA vive en Chip RAM (el DMA lee SPRxDATA/SPRxDATB desde SPRxPT);
///   - se posiciona con SPRxPOS (vstart/hstart) y SPRxCTL (vstop, doble ancho);
///   - usa los colores COLOR16-31 (compartidos entre sprites y, en DPF, con PF2);
///   - en OCS SIEMPRE va delante de los bitplanes (no hay hardware para atrás:
///     un objeto "detrás" se dibuja en un playfield con blit enmascarado).
///
/// El `SpriteManager` es un componente de la escena: guarda la configuración de
/// los 8 sprites, reserva su Chip RAM y emite los MOVEs de Copper
/// (SPRxPT/SPRxPOS/SPRxCTL) dentro de la copperlist que construye el compositor.
///
/// Reglas del engine: sin heap, sin RTTI, gnu++23. La DATA del sprite la aporta
/// la aplicación (bloque Chip); el manager solo la referencia y la posición.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/sprite.hpp>
#include <eng/memory/arena.hpp>

namespace eng::graphics {

/// Configuración de un sprite hardware.
struct SpriteConfig {
    bool enabled = false;
    Span<const u16> data {}; // Chip RAM: `height*2*width_words` words (DAT/DATB)
    u8 width_words = 1;          // 1 = 16 px, 2 = 32 px (SPRxCTL bit doble ancho)
    u8 height = 0;               // líneas (1..128)
    u16 hpos = 0;                // posición horizontal (px)
    u16 vstart = 0;              // línea vertical de inicio
    u16 vstop = 0;               // última línea (vstart + height - 1)
    u8 palette_base = 16;        // COLOR16 + palette_base*4 (defecto 16: COLOR16-19)
};

/// Gestor de hasta 8 sprites hardware (componente de la escena).
class SpriteManager {
public:
    SpriteManager() = default;
    SpriteManager(const SpriteManager&) = delete;
    SpriteManager& operator=(const SpriteManager&) = delete;

    /// Reserva el bloque de DATA de sprites en Chip RAM (la app escribe los
    /// bitmaps con `sprite_data()`). No reserva copper (lo hace el compositor).
    bool init(MemorySystem& memory, u32 data_bytes) {
        m_data = memory.chip.allocate(data_bytes, 16);
        return m_data.valid();
    }

    /// Acceso acotado al bloque de DATA (el tamaño viaja con el puntero).
    Span<u8> sprite_data() { return { static_cast<u8*>(m_data.data), m_data.size }; }
    Span<const u8> sprite_data() const { return { static_cast<const u8*>(m_data.data), m_data.size }; }

    void set(u8 index, const SpriteConfig& cfg) {
        if (index >= 8) return;
        m_spr[index] = cfg;
        // Contrato de tamaño: un sprite habilitado necesita
        // `height*width_words*2` words (DAT+DATB por línea). Si la DATA del span
        // no lo cubre, se descarta (no se emite) en lugar de leer fuera de rango.
        if (cfg.enabled && !cfg.data.empty()) {
            const usize need = static_cast<usize>(cfg.height) * cfg.width_words * 2u;
            if (cfg.data.size() < need) m_spr[index].enabled = false;
        }
    }
    void disable(u8 index) { if (index < 8) m_spr[index] = SpriteConfig{}; }
    void disable_all() { for (auto& s : m_spr) s = SpriteConfig{}; }

    /// ¿Algún sprite habilitado?
    bool any_enabled() const {
        for (const auto& s : m_spr) if (s.enabled) return true;
        return false;
    }

    /// Bits de DMACON que activan el DMA de los sprites habilitados. En OCS el
    /// bit 6 habilita SPR0, el bit 0 SPR7 (DMACON es little-endian de bits).
    u16 dma_bits() const {
        u16 bits = 0;
        for (u8 i = 0; i < 8; ++i) if (m_spr[i].enabled) bits |= static_cast<u16>(1u << (6 - i));
        return bits;
    }

    /// Emite SPRxPT / SPRxPOS / SPRxCTL de los sprites habilitados en el Copper.
    /// Llámala desde el compositor antes de `end()` (paleta y sprites al final).
    void emit_into(copper::Scheduler& sched) const {
        for (u8 i = 0; i < 8; ++i) {
            const SpriteConfig& s = m_spr[i];
            if (!s.enabled || s.data.empty()) continue;
            emit_config(sched, i, s, s.data);
        }
    }

    /// Emite un `SpriteTemplate` con multiplexado vertical y cambios de paleta.
    ///
    /// Toma UNA imagen fuente troceada en segmentos (`SpriteTemplate::segments`) y la
    /// sirve por el MISMO canal hardware: cada segmento se rearma en su linea (técnica
    /// "chasing the raster" / multiplexado vertical). Además aplica los cambios de
    /// paleta por franja (`switches`) escribiendo COLORxx entre medias.
    ///
    /// `channel` es el canal 0..7; `base_y` la primera linea del primer segmento.
    /// Requiere que la `SpriteTemplate` describa segmentos con `data_offset` creciente
    /// (words desde el inicio de `bitmap`); cada linea de sprite ocupa
    /// `width_words*2` words (DAT y DATB intercalados).
    template <u8 MS, u8 MP>
    void emit_template_into(
        copper::Scheduler& sched,
        const SpriteTemplate<MS, MP>& tpl,
        u8 channel,
        u16 base_y,
        u16 hpos = 0
    ) const {
        if (channel >= 8) return;
        u16 line = base_y;
        for (u8 i = 0; i < tpl.segment_count; ++i) {
            const SpriteSegment& seg = tpl.segments[i];
            const Span<const u16> data = tpl.bitmap.subspan(seg.data_offset, seg.height * (tpl.width_words * 2u));
            SpriteConfig cfg {
                true, data, tpl.width_words, static_cast<u8>(seg.height & 0xffu),
                hpos, line, static_cast<u16>(line + seg.height - 1u),
                0, // palette_base: los sprites usan COLOR16+; para multiplexar por par
                   // hay que respetar que el switch cambia el COLORxx del par (ver abajo)
            };
            if (i > 0) {
                // Rearmar UNA línea antes del VSTART. El DMA del sprite arma al
                // inicio de la línea: si escribimos VSTART en la propia línea del
                // segmento, el write llega tarde y se pierde el segmento. El primer
                // segmento NO espera: se programa al inicio de la lista, de modo que
                // el sprite nunca arma con los registros basura que dejó AmigaDOS.
                sched.wait_line(static_cast<u8>((line - 1u) & 0xffu));
            }
            emit_config(sched, channel, cfg, data);

            // Cambios de paleta asociados a este segmento (color multiplexing).
            // Un switch con `line` dentro de [line, line+height) pertenece al segmento
            // que estamos servir: lo aplicamos aqui (el Copper ya espero a esa linea).
            const u16 seg_end = static_cast<u16>(line + seg.height);
            for (u8 s = 0; s < tpl.switch_count; ++s) {
                const SpritePaletteSwitch& sw = tpl.switches[s];
                if (sw.line < line || sw.line >= seg_end) continue;
                for (u8 c = 0; c < sw.count; ++c) {
                    sched.move(
                        static_cast<copper::Register>(0x180 + (sw.first + c) * 2u),
                        sw.colors[c]
                    );
                }
            }
            line = static_cast<u16>(line + seg.height + 1u); // +1 gap requerido por el DMA
        }
    }

    constexpr u16 copper_words() const {
        u16 w = 0;
        for (const auto& s : m_spr) if (s.enabled && !s.data.empty()) w += 6; // CTL+POS+PT(2)
        return w;
    }

private:
    /// Codifica y emite un sprite (CTL/POS/PT) en el canal dado.
    ///
    /// Offsets de registros custom (base $dff000):
    ///   SPRxPOS = 0x140 + x*8, SPRxCTL = 0x142 + x*8, SPRxPTH = 0x120 + x*4,
    ///   SPRxPTL = 0x122 + x*4.
    static void emit_config(copper::Scheduler& sched, u8 channel, const SpriteConfig& s, Span<const u16> data) {
        const u16 pos = static_cast<u16>((static_cast<u16>(s.hpos) << 8) | (s.vstart & 0xff));
        // SPRxCTL: bits 0-7 = vstop, bit 8 = hpos bit 8, bit 9 = vstart bit 8,
        // bit 12 = doble ancho (VSH8), bit 13 = attach.
        const u16 ctl = static_cast<u16>(
            (s.vstop & 0xff) |
            (s.hpos & 0x100) |            // SH1: bit 8 de HSTART
            ((s.vstart >> 8) & 0x200) |
            (s.width_words == 2 ? 0x1000 : 0));
        sched.move(static_cast<copper::Register>(0x140 + channel * 8), pos);     // SPRxPOS (VSTART) primero
        sched.move(static_cast<copper::Register>(0x142 + channel * 8), ctl);     // SPRxCTL (VSTOP) después
        const u32 addr = reinterpret_cast<u32>(data.data());
        sched.move(static_cast<copper::Register>(0x120 + channel * 4), static_cast<u16>(addr >> 16));   // SPRxPTH
        sched.move(static_cast<copper::Register>(0x122 + channel * 4), static_cast<u16>(addr & 0xffff)); // SPRxPTL
    }

    SpriteConfig m_spr[8] {};
    MemoryBlock m_data {};
};

} // namespace eng::graphics
