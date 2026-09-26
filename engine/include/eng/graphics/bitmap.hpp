#pragma once

/// \file bitmap.hpp
/// Capa de memoria: framebuffer hardware (Chip/Fast) + layout + addressing.
///
/// `Bitmap` es el nivel más bajo del diseño: posee un bloque de **Chip RAM**, su
/// geometría (width/height/planes), su layout (interleaved/separate)
/// y el addressing básico (`byte_offset`). NO dibuja ni se muestra: eso lo hace
/// `Playfield` (mapeo + display) y `Surface` (dibujo con clip), que referencian
/// un `Bitmap`.
///
/// Reglas del engine: sin heap (el bloque viene del `MemorySystem`), sin RTTI,
/// gnu++23. Este header es el punto donde vive la memoria; `Playfield` y
/// `Surface` la consumen.
///
/// ```text
///   roles (de abajo arriba)
///   ───────────────────────
///   Bitmap    : memoria + geometría + layout + addressing (byte_offset)   ← ESTE header (no dibuja)
///   Playfield : mapeo lógico→físico + display                             (field/playfield.hpp)
///   Surface   : dibujo con clip                                           (field/surface.hpp)
///
///   Layout  : Interleaved (planelínea = fila*planes + plano) | Separate (plano contiguo)
///   Medio   : Chip (Blitter/bitplanes/Copper por DMA) — un framebuffer Fast no es mostrable
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/plane_layout.hpp>
#include <eng/memory/arena.hpp>

namespace eng::gfx {

/// Layout de los bitplanes (alias del enum único `eng::graphics::PlaneLayout`; `Separate` =
/// planos contiguos, el vocabulario histórico de `Bitmap`).
using PlaneLayout = eng::graphics::PlaneLayout;

/// Configuración de un framebuffer.
struct BitmapConfig {
    u16 width = 0;
    u16 height = 0;
    u8 planes = 4;
    PlaneLayout layout = PlaneLayout::Interleaved;
    u16 row_bytes = 0;        // 0 = auto (width/8)
    u16 alignment = 16;
    u16 frontbase_offset = 0; // bytes del bloque ANTES del frontbuffer (fetch ancho: 0/16/48)
    u32 guard_bytes = 0;      // reserva extra tras total_bytes (guarda de DMA)
};

/// Framebuffer hardware: memoria + layout + addressing. SIN dibujo.
class Bitmap {
public:
    Bitmap() = default;
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;

    /// Reserva el bloque **siempre en Chip RAM** (el Blitter y el bitplane DMA solo alcanzan
    /// Chip; un framebuffer Fast no es mostrable). El bloque reservado mide
    /// `total_bytes + guard_bytes`; `bytes()`/frontbuffer apuntan a `base + frontbase_offset`
    /// (el offset de fetch ancho del corkscrew: normal=0, BPL32=16, 4x=48). La guardia protege
    /// las lecturas DMA/blits que rebasan el final lógico del framebuffer.
    bool init(MemorySystem& memory, const BitmapConfig& cfg) {
        if (cfg.width == 0 || cfg.height == 0 || cfg.planes == 0 || cfg.planes > 6) return false;
        m_cfg = cfg;
        m_row_bytes = cfg.row_bytes ? cfg.row_bytes : static_cast<u16>(cfg.width / 8u);
        m_total = static_cast<u32>(m_row_bytes) * cfg.height * cfg.planes;
        const u32 alloc = m_total + cfg.guard_bytes;
        m_block = memory.chip.allocate_block<eng::PlaneTag>(alloc, cfg.alignment);
        if (!m_block.valid()) return false;
        m_real_base = m_block.mem_view_chip().address();
        m_frontbuffer = m_real_base + cfg.frontbase_offset;
        return true;
    }

    constexpr bool valid() const { return m_block.valid(); }

    /// Base del bloque completo (AllocBitMap), la que usa el Copper para BPLxPT
    /// en modos con offset de fetch. USO INTERNO del engine (hardware_view).
    [[nodiscard]] constexpr Address<MemoryKind::Chip> allocation_start() const { return m_real_base; }
    /// Base del bloque (lo que va a `BPLxPT`), como `Address<Chip>`. El rol lo da el nombre.
    [[nodiscard]] constexpr Address<MemoryKind::Chip> base() const { return m_real_base; }
    /// Buffer de escritura (con `frontbase_offset`), como `Address<Chip>`.
    [[nodiscard]] constexpr Address<MemoryKind::Chip> front() const { return m_frontbuffer; }
    /// Planos como vista **certificada en Chip** (lo que consume el Copper/`BPLxPT`).
    [[nodiscard]] constexpr ChipView<PlaneTag> chip_planes() const {
        return ChipView<PlaneTag> { m_real_base, static_cast<usize>(m_total) };
    }
    constexpr u16 frontbase_offset() const { return m_cfg.frontbase_offset; }

    /// Vista acotada del bloque (el tamaño viaja con el puntero). NO es la vía
    /// de dibujo: toda escritura pública pasa por `Surface`/blits con `Span`.
    /// `bytes()` existe para generación de contenido por CPU (procedural) y para
    /// leer el framebuffer; internamente el engine usa la vía cruda optimizada.
    Span<u8> bytes() { return { m_frontbuffer.ptr(), m_total }; }
    Span<const u8> bytes() const { return { m_frontbuffer.cptr(), m_total }; }

    constexpr u16 width() const { return m_cfg.width; }
    constexpr u16 height() const { return m_cfg.height; }
    constexpr u8 planes() const { return m_cfg.planes; }
    constexpr u16 row_bytes() const { return m_row_bytes; }
    constexpr u32 total_bytes() const { return m_total; }
    constexpr PlaneLayout layout() const { return m_cfg.layout; }
    /// Un `Bitmap` vive siempre en Chip RAM: el Blitter y el bitplane DMA lo alcanzan.
    constexpr bool blitter_accessible() const { return true; }

    /// Offset físico (bytes) del píxel (x, y) en el plano `plane`, según el layout.
    /// Interleaved: `(y*planes + plane)*row_bytes + x/8`.
    /// Separate:    `plane*(row_bytes*height) + y*row_bytes + x/8`.
    constexpr u32 byte_offset(u16 x, u8 y, u8 plane) const {
        const u32 row = m_row_bytes;
        if (m_cfg.layout == PlaneLayout::Separate) {
            return static_cast<u32>(plane) * row * m_cfg.height + static_cast<u32>(y) * row + (x / 8u);
        }
        return (static_cast<u32>(y) * m_cfg.planes + plane) * row + (x / 8u);
    }

private:
    eng::Block<eng::PlaneTag> m_block {};
    BitmapConfig m_cfg {};
    Address<MemoryKind::Chip> m_real_base {};
    Address<MemoryKind::Chip> m_frontbuffer {};
    u16 m_row_bytes = 0;
    u32 m_total = 0;
};

} // namespace eng::gfx