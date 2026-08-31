#pragma once

/// \file bitmap.hpp
/// Capa de memoria: framebuffer hardware (Chip/Fast) + layout + addressing.
///
/// `Bitmap` es el nivel más bajo del diseño: posee un bloque de RAM (chip o
/// fast), su geometría (width/height/planes), su layout (interleaved/separate)
/// y el addressing básico (`byte_offset`). NO dibuja ni se muestra: eso lo hace
/// `Playfield` (mapeo + display) y `Surface` (dibujo con clip), que referencian
/// un `Bitmap`.
///
/// Reglas del engine: sin heap (el bloque viene del `MemorySystem`), sin RTTI,
/// gnu++23. Este header es el punto donde vive la memoria; `Playfield` y
/// `Surface` la consumen.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/memory/arena.hpp>

namespace eng::gfx {

/// Layout de los bitplanes en memoria.
enum class PlaneLayout : u8 {
    Interleaved,  // planelínea = fila*planes + plano; direccionar 1 fila por planelínea
    Separate,     // cada plano contiguo: addr = plano*row_bytes*height + fila*row_bytes + x/8
};

/// Dominio de memoria (determina quién puede acceder por DMA).
enum class MemoryDomain : u8 {
    Chip,  // accesible por Blitter, bitplane DMA, audio DMA, Copper
    Fast,  // solo CPU (los blits software_copy la usan)
    Any,   // el sistema decide
};

/// Configuración de un framebuffer.
struct BitmapConfig {
    u16 width = 0;
    u16 height = 0;
    u8 planes = 4;
    PlaneLayout layout = PlaneLayout::Interleaved;
    u16 row_bytes = 0;      // 0 = auto (width/8)
    u16 alignment = 16;
    MemoryDomain domain = MemoryDomain::Chip;
};

/// Framebuffer hardware: memoria + layout + addressing. SIN dibujo.
class Bitmap {
public:
    Bitmap() = default;
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;

    /// Reserva el bloque en el dominio pedido (Chip o Fast).
    bool init(MemorySystem& memory, const BitmapConfig& cfg) {
        if (cfg.width == 0 || cfg.height == 0 || cfg.planes == 0 || cfg.planes > 6) return false;
        m_cfg = cfg;
        m_row_bytes = cfg.row_bytes ? cfg.row_bytes : static_cast<u16>(cfg.width / 8u);
        m_total = static_cast<u32>(m_row_bytes) * cfg.height * cfg.planes;
        m_block = (cfg.domain == MemoryDomain::Fast)
            ? memory.slow.allocate(m_total, cfg.alignment)
            : memory.chip.allocate(m_total, cfg.alignment);
        if (!m_block.valid()) return false;
        m_frontbuffer = static_cast<u8*>(m_block.data);
        return true;
    }

    constexpr bool valid() const { return m_block.valid(); }

    /// Vista acotada del bloque (el tamaño viaja con el puntero). NO es la vía
    /// de dibujo: toda escritura pública pasa por `Surface`/blits con `Span`.
    /// `bytes()` existe para generación de contenido por CPU (procedural) y para
    /// leer el framebuffer; internamente el engine usa la vía cruda optimizada.
    Span<u8> bytes() { return { m_frontbuffer, m_total }; }
    Span<const u8> bytes() const { return { m_frontbuffer, m_total }; }

    constexpr u16 width() const { return m_cfg.width; }
    constexpr u16 height() const { return m_cfg.height; }
    constexpr u8 planes() const { return m_cfg.planes; }
    constexpr u16 row_bytes() const { return m_row_bytes; }
    constexpr u32 total_bytes() const { return m_total; }
    constexpr PlaneLayout layout() const { return m_cfg.layout; }
    constexpr MemoryDomain domain() const { return m_cfg.domain; }
    /// ¿El Blitter puede acceder a este bitmap? (solo Chip)
    constexpr bool blitter_accessible() const { return m_cfg.domain == MemoryDomain::Chip; }

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
    MemoryBlock m_block {};
    BitmapConfig m_cfg {};
    u8* m_frontbuffer = nullptr;
    u16 m_row_bytes = 0;
    u32 m_total = 0;
};

} // namespace eng::gfx