#pragma once

/// \file amiga_display_mapper.hpp
/// Mapeo NEUTRAL de la cámara de un scroll "flat" (bitmap contiguo, sin anillo ni
/// split) a los registros que consume el compositor de Amiga. Es la traducción
/// cámara→hardware aislada del playfield y del Copper, como pide el modelo
/// objetivo (`docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md` §5).
///
/// Geometría: fetch ancho (`DDFSTRT=$30`, 40 bytes visibles + 2 de guarda). La
/// fórmula es la del driver lineal ya verificado
/// (`engine/include/eng/graphics/drivers/tile_scroll.hpp`):
///
///   fine      = cam_x & 15
///   BPLCON1   = (16-fine) & 15, duplicado en los dos nibbles
///   planeaddx = ((cam_x-1) & ~15) / 8      // una word antes solo cuando fine==0
///   planeaddy = cam_y * planes * row_bytes // interleaved
///   BPLMOD    = row_bytes*planes - fetch_bytes
///
/// El mínimo de `cam_x` es 1: con el fetch adelantado, `cam_x==0` haría que el
/// puntero apuntase antes del bitmap. Ver AMIGA_8WAY_SCROLLING.md §7.

#include <eng/core/math/fast_div.hpp>
#include <eng/core/types/types.hpp>

namespace eng::field {

/// Resultado del mapeo del corkscrew/XYLimited (anillo + staging + split).
struct RingDisplayMapping {
    u32 planeaddx = 0;
    u16 bplcon1 = 0;
    u32 planeaddy = 0;
    u16 display_offset = 0;
    u16 split_line = 0;
    bool split_active = false;
};

/// Traduce la cámara del corkscrew/XYLimited a los registros del display. Réplica
/// exacta de `UpdateCopperlist` (xlimited.c) que usaba `XLimitedPlayfield`.
///
/// `DisplayHeight` como NTTP permite que el módulo y el `split_line` se resuelvan
/// en compile-time (`fast_div`, sin `__umodsi3`); con `0` se usa el `display_h`
/// runtime (misma semántica, división nativa). `fetch_pixels` (16/32/64) se
/// mantiene runtime porque sale de la config (`fetch_mode`), igual que en el
/// playfield original.
///
/// - `fine = (I-1) - (xpos & (I-1))`, con `xpos = videoposx + I - 1`; los bits 16/32
///   de `fine` activan el fetch ancho (`0x4400`/`0x8800` en `BPLCON1`).
/// - `display_offset = (videoposy + tile_h) % display_height` (solo si hay Y).
/// - `split_line = display_height - display_offset`;
///   `split_active = !linear && scroll_y && split_line < viewport_h`.
template <u32 DisplayHeight = 0u>
constexpr RingDisplayMapping map_ring_scroll(
    s32 videoposx, s32 videoposy, u16 fetch_pixels, u16 tile_h,
    u8 planes, u16 row_bytes, u16 display_h, u16 viewport_h,
    bool scroll_y, bool linear_display
) {
    RingDisplayMapping m;
    const s32 I = static_cast<s32>(fetch_pixels);
    const s32 xpos = videoposx + I - 1;
    m.planeaddx = static_cast<u32>(xpos / I) * static_cast<u32>(I / 8);
    const s32 fine = (I - 1) - (xpos & (I - 1));
    u16 scroll = static_cast<u16>((fine & 15) * 0x11);
    if (fine & 16) scroll |= 0x4400;
    if (fine & 32) scroll |= 0x8800;
    m.bplcon1 = scroll;

    u16 display_offset = 0;
    if (scroll_y) {
        const s32 dh = static_cast<s32>(DisplayHeight != 0u ? DisplayHeight : display_h);
        const s32 vy = (videoposy % dh + dh) % dh; // dmod2 (robusto a negativos)
        if constexpr (DisplayHeight != 0u) {
            display_offset = static_cast<u16>(
                eng::fast_div<DisplayHeight>::r(static_cast<u32>(vy + static_cast<s32>(tile_h))));
        } else {
            display_offset = static_cast<u16>(
                (static_cast<u32>(vy + static_cast<s32>(tile_h))) % static_cast<u32>(dh));
        }
    }
    m.display_offset = display_offset;
    m.planeaddy = static_cast<u32>(display_offset) * planes * row_bytes;
    const u16 dh16 = static_cast<u16>(DisplayHeight != 0u ? DisplayHeight : display_h);
    m.split_line = static_cast<u16>(dh16 - display_offset);
    m.split_active = !linear_display && scroll_y && m.split_line < viewport_h;
    return m;
}

/// Resultado del mapeo, en las unidades que espera `PlayfieldHardwareView`.
struct FlatDisplayMapping {
    u32 planeaddx = 0; // coarse X en bytes
    u32 planeaddy = 0; // offset Y interleaved (filas * planes * row_bytes)
    u16 bplcon1 = 0;   // fine duplicado en ambos nibbles
    u16 bpl1mod = 0;   // módulo de bitplane (BPL1MOD = BPL2MOD)
};

/// Traduce la cámara de un bitmap flat a los registros del display.
///
/// `cam` puede ir fuera de rango: `cam_x` se clampa a `min_cam_x` y `cam_y` no
/// se valida aquí (los límites del mundo los conoce la superficie).
constexpr FlatDisplayMapping map_flat_scroll(
    s32 cam_x, s32 cam_y, u16 row_bytes, u8 planes,
    u16 fetch_bytes = 42u, s32 min_cam_x = 1
) {
    const s32 cx = cam_x < min_cam_x ? min_cam_x : cam_x;
    const u16 fine = static_cast<u16>(cx & 15);
    const u16 nibble = static_cast<u16>((16u - fine) & 15u);
    FlatDisplayMapping m;
    m.planeaddx = static_cast<u32>((cx - 1) & ~15) / 8u;
    m.planeaddy = static_cast<u32>(cam_y) * static_cast<u32>(planes) * row_bytes;
    m.bplcon1 = static_cast<u16>(nibble | (nibble << 4));
    m.bpl1mod = static_cast<u16>(static_cast<u32>(row_bytes) * planes - fetch_bytes);
    return m;
}

} // namespace eng::field
