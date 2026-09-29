#pragma once

/// \file flat_playfield.hpp
/// Superficie "virtual playfield": un bitmap FLAT contiguo (mundo pre-dibujado)
/// con cámara 2D; el scroll solo mueve punteros (cero blits por frame). La
/// estrategia es `BigBufferScroll` (una instancia por eje; el struct modela un
/// solo eje) y el mapeo a registros lo aporta `map_flat_scroll`.
///
/// Compone superficie (CanvasPlayfield, layout interleaved) + estrategia
/// (BigBufferScroll) + vista (PlayfieldHardwareView) sin que la demo conozca
/// registros, según `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md`.
/// Verificada por la demo `demos/techniques/amiga/playfield/120_virtual_playfield`.

#include <eng/core/types/types.hpp>
#include <eng/field/amiga_display_mapper.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/scroll_engine.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

/// Configuración del virtual playfield: tamaño del mundo, ventana visible y
/// profundidad. `fetch_bytes` es el fetch del DDF programado (42 con $30).
struct FlatScrollConfig {
    u16 world_w = 0;
    u16 world_h = 0;
    u16 view_w = 320;
    u16 view_h = 256;
    u8 planes = 4;
    u16 fetch_bytes = 42;
};

class FlatScrollPlayfield : public CanvasPlayfield {
public:
    /// Reserva el bitmap del mundo en Chip RAM y fija los límites de cámara
    /// (X `[1, world_w-view_w]`, Y `[0, world_h-view_h]`, saturados).
    bool begin(MemorySystem& memory, const FlatScrollConfig& cfg) {
        if (cfg.world_w == 0 || cfg.world_h == 0 || cfg.view_w == 0 || cfg.view_h == 0) return false;
        if (cfg.world_w < cfg.view_w || cfg.world_h < cfg.view_h) return false;
        if (!CanvasPlayfield::begin(memory, {cfg.world_w, cfg.world_h, cfg.planes})) return false;
        m_cfg = cfg;
        m_cam_x.min_pos = 1;
        m_cam_x.max_pos = static_cast<s32>(cfg.world_w - cfg.view_w);
        m_cam_y.min_pos = 0;
        m_cam_y.max_pos = static_cast<s32>(cfg.world_h - cfg.view_h);
        return true;
    }

    BigBufferScroll& cam_x() { return m_cam_x; }
    BigBufferScroll& cam_y() { return m_cam_y; }
    const BigBufferScroll& cam_x() const { return m_cam_x; }
    const BigBufferScroll& cam_y() const { return m_cam_y; }

    PlayfieldHardwareView hardware_view() const override {
        PlayfieldHardwareView v = CanvasPlayfield::hardware_view();
        const s32 cx = m_cam_x.position;
        const s32 cy = m_cam_y.position;
        const FlatDisplayMapping m =
            map_flat_scroll(cx, cy, bytes_per_row(), m_cfg.planes, m_cfg.fetch_bytes);
        v.planeaddx = m.planeaddx;
        v.planeaddy = m.planeaddy;
        v.bplcon1 = m.bplcon1;
        v.bpl1mod = m.bpl1mod;
        v.bpl2mod = m.bpl1mod;
        v.viewport_w = m_cfg.view_w;
        v.viewport_h = m_cfg.view_h;
        v.display_height = m_cfg.world_h;
        v.display_offset = 0;
        v.split_line = 0;
        v.split_active = false;
        v.videoposx = cx;
        v.mapposx = cx;
        v.videoposy = cy;
        v.mapposy = cy;
        return v;
    }

    s32 mapposx() const override { return m_cam_x.position; }
    s32 mapposy() const override { return m_cam_y.position; }

private:
    FlatScrollConfig m_cfg {};
    BigBufferScroll m_cam_x {};
    BigBufferScroll m_cam_y {};
};

} // namespace eng::field
