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

#include <eng/core/types.hpp>

namespace eng::field {

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
