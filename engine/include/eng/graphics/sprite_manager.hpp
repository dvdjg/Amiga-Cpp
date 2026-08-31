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

#include <eng/core/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/arena.hpp>

namespace eng::graphics {

/// Configuración de un sprite hardware.
struct SpriteConfig {
    bool enabled = false;
    const u16* data = nullptr;   // Chip RAM: `height*2*width_words` words (DAT/DATB por línea)
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

    u8* sprite_data() { return static_cast<u8*>(m_data.data); }
    const u8* sprite_data() const { return static_cast<const u8*>(m_data.data); }

    void set(u8 index, const SpriteConfig& cfg) {
        if (index < 8) m_spr[index] = cfg;
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
            if (!s.enabled || s.data == nullptr) continue;
            const u16 pos = static_cast<u16>((static_cast<u16>(s.hpos) << 8) | (s.vstart & 0xff));
            // SPRxCTL: bits 0-7 = vstop, bit 8 = hpos bit 8, bit 9 = vstart bit 8,
            // bit 12 = doble ancho (VSH8), bit 13 = attach.
            const u16 ctl = static_cast<u16>(
                (s.vstop & 0xff) |
                ((s.hpos >> 1) & 0x100) |
                ((s.vstart >> 8) & 0x200) |
                (s.width_words == 2 ? 0x1000 : 0));
            sched.move(static_cast<copper::Register>(0x0d0 + i * 8), ctl);     // SPRxCTL
            sched.move(static_cast<copper::Register>(0x0d2 + i * 8), pos);     // SPRxPOS
            const u32 addr = reinterpret_cast<u32>(s.data);
            sched.move(static_cast<copper::Register>(0x120 + i * 4), static_cast<u16>(addr >> 16));
            sched.move(static_cast<copper::Register>(0x122 + i * 4), static_cast<u16>(addr & 0xffff));
        }
    }

    constexpr u16 copper_words() const {
        u16 w = 0;
        for (const auto& s : m_spr) if (s.enabled && s.data) w += 6; // CTL+POS+PT(2)
        return w;
    }

private:
    SpriteConfig m_spr[8] {};
    MemoryBlock m_data {};
};

} // namespace eng::graphics
