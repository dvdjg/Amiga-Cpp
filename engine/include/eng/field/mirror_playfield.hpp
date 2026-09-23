#pragma once

/// \file mirror_playfield.hpp
/// Superficie de scroll con **espejo vertical**: el bitmap contiene el bucle de
/// display DUPLICADO (filas `[0, display_h)` y su copia en `[display_h, 2*display_h)`),
/// de modo que el display lee de forma **contigua** desde la cámara y **no hace
/// falta split de Copper**. Es la alternativa recomendada para un viewport de
/// scroll de 256 px, donde el comparador de 8 bits del Copper no permite un split
/// móvil fiable (`docs/guides/roadmap/CONSULTA-SPLIT-208.md`).
///
/// El mapeo horizontal es el del scroll flat (`map_flat_scroll`); lo propio del
/// espejo es la superficie (duplicado del bucle) y la cámara Y envolvente.
/// Verificada por la demo `demos/amiga/121_mirror_scroll`.

#include <eng/core/types/types.hpp>
#include <eng/field/amiga_display_mapper.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/scroll_engine.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

struct MirrorScrollConfig {
    u16 world_w = 0;    // ancho del bitmap (mundo horizontal)
    u16 view_w = 320;   // ventana visible
    u16 view_h = 256;
    u16 display_h = 0;  // alto del bucle de display (se duplica en el bitmap)
    u8 planes = 4;
    u16 fetch_bytes = 42;
};

class MirrorScrollPlayfield : public CanvasPlayfield {
public:
    /// Reserva el bitmap `world_w x (2*display_h)` y fija la cámara X (saturada)
    /// y la cámara Y (envolvente dentro del bucle).
    bool begin(MemorySystem& memory, const MirrorScrollConfig& cfg) {
        if (cfg.world_w == 0 || cfg.display_h == 0 || cfg.view_w == 0 || cfg.view_h == 0) return false;
        if (cfg.world_w < cfg.view_w || cfg.display_h < cfg.view_h) return false;
        const u16 bitmap_h = static_cast<u16>(cfg.display_h * 2u);
        if (!CanvasPlayfield::begin(memory, {cfg.world_w, bitmap_h, cfg.planes})) return false;
        m_cfg = cfg;
        m_cam_x.min_pos = 1;
        m_cam_x.max_pos = static_cast<s32>(cfg.world_w - cfg.view_w);
        m_cam_y = 0;
        m_dir_x = 1;
        return true;
    }

    /// Avanza la cámara: X satura y reveer; Y envuelve dentro del bucle.
    void advance(s32 dx, s32 dy) {
        const s32 before = m_cam_x.position;
        m_cam_x.step(dx);
        if (m_cam_x.position == before) m_dir_x = -m_dir_x;
        const s32 dh = static_cast<s32>(m_cfg.display_h);
        m_cam_y = (m_cam_y + dy) % dh;
        if (m_cam_y < 0) m_cam_y += dh;
    }

    s32 cam_x() const { return m_cam_x.position; }
    s32 cam_y() const { return m_cam_y; }

    PlayfieldHardwareView hardware_view() const override {
        PlayfieldHardwareView v = CanvasPlayfield::hardware_view();
        // El mapeo horizontal (y planeaddy = cam_y*planes*row) es el del flat: el
        // espejo permite leer contiguo desde cam_y sin envolver el puntero.
        const FlatDisplayMapping m =
            map_flat_scroll(m_cam_x.position, m_cam_y, bytes_per_row(), m_cfg.planes, m_cfg.fetch_bytes);
        v.planeaddx = m.planeaddx;
        v.planeaddy = m.planeaddy;
        v.bplcon1 = m.bplcon1;
        v.bpl1mod = m.bpl1mod;
        v.bpl2mod = m.bpl1mod;
        v.viewport_w = m_cfg.view_w;
        v.viewport_h = m_cfg.view_h;
        v.display_height = m_cfg.display_h;
        v.display_offset = 0;
        v.split_line = 0;
        v.split_active = false; // el wrap lo resuelve el espejo, no el Copper
        v.videoposx = m_cam_x.position;
        v.mapposx = m_cam_x.position;
        v.videoposy = m_cam_y;
        v.mapposy = m_cam_y;
        return v;
    }

    s32 mapposx() const override { return m_cam_x.position; }
    s32 mapposy() const override { return m_cam_y; }

private:
    MirrorScrollConfig m_cfg {};
    BigBufferScroll m_cam_x {};
    s32 m_cam_y = 0;
    s32 m_dir_x = 1;
};

} // namespace eng::field
