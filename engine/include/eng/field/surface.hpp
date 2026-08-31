#pragma once

/// \file surface.hpp
/// Contexto de dibujo con recorte (clip) sobre un `Playfield`.
///
/// Según el diseño del engine, `Playfield` NO dibuja: representa el framebuffer
/// hardware + su mapeo lógico→físico. `Surface` es el único contexto de dibujo:
/// una subregión rectangular (origen + tamaño + clip) sobre un playfield, con
/// las primitivas (`set_pixel`, `fill_rect`, `draw_line`, `blit`, `blit_masked`)
/// recortadas contra su clip y enrutadas por el mapeo del playfield.
///
/// Es la base del GUI: un `Widget` es una `Surface` + `draw()` + `hit_test(punto)`.
///
/// `Surface` es un tipo VALOR (no propietario): se construye sobre la marcha
/// apuntando a un playfield (`Playfield*` no-propietario; `Ref` es un
/// refactor pendiente). La escena expone superficies listas (`bg_surface()`,
/// `hud_surface()`) para el código de juego.

#include <eng/field/playfield.hpp>

namespace eng::field {

/// Rectángulo en el espacio lógico del playfield (mundo o pantalla).
struct SurfaceRect {
    s32 x = 0;
    s32 y = 0;
    u16 w = 0;
    u16 h = 0;
};

/// Contexto de dibujo con clip sobre un playfield.
class Surface {
public:
    Surface() = default;
    Surface(Playfield& target, const SurfaceRect& clip)
        : m_target(&target), m_clip(clip) {}

    constexpr bool valid() const { return m_target != nullptr; }
    constexpr const Playfield* target() const { return m_target; }
    constexpr const SurfaceRect& clip() const { return m_clip; }

    /// ¿El punto (mundo/pantalla) está dentro del clip?
    constexpr bool contains(s32 x, s32 y) const {
        return x >= m_clip.x && y >= m_clip.y &&
               static_cast<u32>(x - m_clip.x) < m_clip.w &&
               static_cast<u32>(y - m_clip.y) < m_clip.h;
    }

    /// Píxel. Recorta contra el clip y escribe vía el mapeo del playfield.
    bool set_pixel(s32 x, s32 y, u8 color) {
        if (!valid() || !contains(x, y)) return false;
        return m_target->write_pixel(x, y, color);
    }

    /// Rectángulo relleno (CPU), recortado. true si todo el rect estaba dentro.
    bool fill_rect(s32 x, s32 y, u16 w, u16 h, u8 color) {
        if (!valid()) return false;
        bool ok = true;
        for (u16 dy = 0; dy < h; ++dy)
            for (u16 dx = 0; dx < w; ++dx)
                if (!set_pixel(x + dx, y + dy, color)) ok = false;
        return ok;
    }

    /// Línea oblicua (Bresenham, CPU), recortada.
    bool draw_line(s32 x0, s32 y0, s32 x1, s32 y1, u8 color) {
        if (!valid()) return false;
        const s32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
        const s32 dy = y1 > y0 ? y1 - y0 : y0 - y1;
        const s32 sx = x0 < x1 ? 1 : -1;
        const s32 sy = y0 < y1 ? 1 : -1;
        s32 err = dx - dy;
        bool ok = true;
        for (;;) {
            if (!set_pixel(x0, y0, color)) ok = false;
            if (x0 == x1 && y0 == y1) break;
            const s32 e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx)  { err += dx; y0 += sy; }
        }
        return ok;
    }

    /// Blit planar en el mundo (delega en el playfield; la costura/espejo las
    /// gestiona el layout). Recorta el rect contra el clip. La fuente viaja como
    /// `Span` (el tamaño es el contrato que el playfield valida).
    bool blit(graphics::FramePlan& plan, Span<const u16> src, s32 x, s32 y,
              u16 w, u16 h, u16 src_row_bytes, u32 src_plane_stride, u8 planes) {
        if (!valid() || x < m_clip.x || y < m_clip.y ||
            x + static_cast<s32>(w) > m_clip.x + m_clip.w ||
            y + static_cast<s32>(h) > m_clip.y + m_clip.h) return false;
        return m_target->add_world_bitmap(plan, src, x, y, w, h,
                                          src_row_bytes, src_plane_stride, planes);
    }

    /// BOB enmascarado (cookie-cut) en el mundo, recortado contra el clip.
    bool blit_masked(graphics::FramePlan& plan, Span<const u16> src, Span<const u16> mask,
                     s32 x, s32 y, u16 w, u16 h,
                     u16 src_row_bytes, u32 src_plane_stride, u8 planes) {
        if (!valid() || x < m_clip.x || y < m_clip.y ||
            x + static_cast<s32>(w) > m_clip.x + m_clip.w ||
            y + static_cast<s32>(h) > m_clip.y + m_clip.h) return false;
        return m_target->add_world_bitmap_masked(plan, src, mask, x, y, w, h,
                                                 src_row_bytes, src_plane_stride, planes);
    }

private:
    Playfield* m_target = nullptr; // no-propietario; Ref es un refactor pendiente
    SurfaceRect m_clip {};
};

} // namespace eng::field
