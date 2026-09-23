#pragma once

/// \file double_buffer_playfield.hpp
/// Superficie de scroll con **doble buffer de bitmap**: dos bitmaps completos del
/// mundo; el display lee el delantero mientras la aplicación escribe en el
/// trasero, y `flip()` conmuta (el compositor instala la copperlist del nuevo
/// delantero, swap de `COP1LC`). Elimina el tearing de cualquier escritura sobre
/// filas visibles (a diferencia del bitmap único, que solo puede escribir en la
/// banda de staging).
///
/// **No posee memoria**: `bind` la liga a **dos bitmaps del display** que reserva el llamador
/// (la escena/el display poseen los buffers; la superficie solo escribe y conmuta). El scroll por
/// punteros lo aporta el mapper flat (`map_flat_scroll`). Verificada por la demo
/// `demos/amiga/122_doublebuffer_scroll`.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/amiga_display_mapper.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/scroll_engine.hpp>
#include <eng/graphics/bitmap.hpp>

namespace eng::field {

struct DoubleBufferScrollConfig {
    u16 world_w = 0;
    u16 world_h = 0;
    u16 view_w = 320;
    u16 view_h = 256;
    u8 planes = 4;
    u16 fetch_bytes = 42;
};

class DoubleBufferScrollPlayfield {
public:
    /// Liga la superficie a **dos bitmaps del display** (no los posee; los reserva el llamador) y
    /// fija la configuración de scroll. `false` si la geometría no cuadra o los bitmaps no son
    /// válidos.
    bool bind(const DoubleBufferScrollConfig& cfg, gfx::Bitmap& b0, gfx::Bitmap& b1) {
        if (cfg.world_w == 0 || cfg.world_h == 0 || cfg.view_w == 0 || cfg.view_h == 0) return false;
        if (cfg.world_w < cfg.view_w || cfg.world_h < cfg.view_h) return false;
        if (b0.row_bytes() == 0 || b1.row_bytes() == 0) return false;
        m_buf[0] = b0;
        m_buf[1] = b1;
        m_cfg = cfg;
        m_cam_x.min_pos = 1;
        m_cam_x.max_pos = static_cast<s32>(cfg.world_w - cfg.view_w);
        m_cam_y.min_pos = 0;
        m_cam_y.max_pos = static_cast<s32>(cfg.world_h - cfg.view_h);
        m_front = 0;
        return true;
    }

    /// Base de escritura del buffer `idx` (0/1). El llamador pinta el mundo en los
    /// DOS buffers antes de empezar a mostrar (init).
    u8* buffer_bytes(u8 idx) { return m_buf[idx & 1u].get()->bytes().data(); }
    u16 row_bytes() const { return m_buf[0].get()->row_bytes(); }
    u8 front_index() const { return m_front; }

    /// Conmuta el buffer delantero (el compositor leerá el nuevo).
    void flip() { m_front ^= 1u; }

    BigBufferScroll& cam_x() { return m_cam_x; }
    BigBufferScroll& cam_y() { return m_cam_y; }
    const BigBufferScroll& cam_x() const { return m_cam_x; }
    const BigBufferScroll& cam_y() const { return m_cam_y; }

    PlayfieldHardwareView hardware_view() const {
        const gfx::Bitmap& b = *m_buf[m_front].get();
        const u8* const base = b.bytes().data();
        const FlatDisplayMapping m =
            map_flat_scroll(m_cam_x.position, m_cam_y.position, b.row_bytes(), b.planes(), m_cfg.fetch_bytes);
        PlayfieldHardwareView v {};
        v.bitplanes = base;
        v.real_base = base;
        v.bitmap_bytes_per_row = b.row_bytes();
        v.plane_bytes = b.total_bytes();
        v.planes = b.planes();
        v.bitmap_height = b.height();
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
        v.videoposx = m_cam_x.position;
        v.mapposx = m_cam_x.position;
        v.videoposy = m_cam_y.position;
        v.mapposy = m_cam_y.position;
        return v;
    }

private:
    DoubleBufferScrollConfig m_cfg {};
    eng::Ref<gfx::Bitmap> m_buf[2] {}; ///< bitmaps del display (no propietarios)
    u8 m_front = 0;
    BigBufferScroll m_cam_x {};
    BigBufferScroll m_cam_y {};
};

} // namespace eng::field

