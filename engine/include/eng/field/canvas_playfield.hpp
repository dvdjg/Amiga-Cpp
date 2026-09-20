#pragma once

/// \file canvas_playfield.hpp
/// Lienzo planar **interleaved** sin tiles ni scroll (HUD, fondo estático, capa de
/// actores). Definido aparte de `playfield_base.hpp`; `playfield.hpp` es la cabecera
/// de familia que reúne base e implementaciones.

#include <eng/field/playfield_base.hpp>

namespace eng::field {

/// Lienzo plano: un playfield SIN tiles ni scroll, para blits y primitivas de
/// CPU. Es la base de un HUD, de un fondo estático o de una capa de actores.
/// Layout interleaved (`frontbuffer + planelínea*row + p*row`), sin walk, sin
/// espejo y sin costura: cada píxel vive en su fila.
class CanvasPlayfield : public Playfield {
public:
    struct Config {
        u16 width = 320;      // ancho visible en píxeles
        u16 height = 32;      // alto en filas (p. ej. franja HUD)
        u8 planes = 4;        // nº de planos de bitplane
    };

    /// Reserva el framebuffer en Chip RAM (interleaved, `width/8*height*planes`).
    bool begin(MemorySystem& memory, const Config& cfg) {
        if (cfg.width == 0 || cfg.height == 0 || cfg.planes == 0 || cfg.planes > 6) return false;
        gfx::BitmapConfig bc;
        bc.width = cfg.width;
        bc.height = cfg.height;
        bc.planes = cfg.planes;
        bc.layout = gfx::PlaneLayout::Interleaved;
        if (!m_bitmap.init(memory, bc)) return false;
        sync_from_bitmap();
        u8* d = m_frontbuffer;
        for (u32 i = 0; i < m_total_bytes; ++i) d[i] = 0;
        m_initialized = true;
        return true;
    }

    /// Construye el lienzo sobre bitplanes **ya reservados** (sin reservar memoria), con
    /// el mismo mapeo que `begin`. Separa la propiedad de la memoria de la emisión, de
    /// modo que el llamador puede repartir N buffers (p. ej. `MultiBuffered<CanvasScene, N>`).
    /// `bitplanes.view.size()` debe cubrir `(width/8 & ~1) * planes * height`.
    bool bind(eng::Block<eng::PlaneTag> bitplanes, const Config& cfg) {
        if (!bitplanes.valid() || cfg.width == 0u || cfg.height == 0u || cfg.planes == 0u ||
            cfg.planes > 6u) {
            return false;
        }
        const u16 row = static_cast<u16>((cfg.width / 8u) & ~1u);
        const u32 need = static_cast<u32>(row) * cfg.planes * cfg.height;
        if (static_cast<u32>(bitplanes.view.size()) < need) return false;
        m_bound = bitplanes;
        m_width = cfg.width;
        m_height = cfg.height;
        m_planes = cfg.planes;
        m_bytes_per_row = row;
        m_total_bytes = need;
        m_frontbuffer = bitplanes.view.data(); // vía cruda interna (núcleo)
        m_initialized = true;
        return true;
    }

    /// Planos del lienzo cuando se construyó con `bind` (vacío si se usó `begin`, cuyo
    /// framebuffer propietario se lee con `bitmap()`).
    [[nodiscard]] constexpr eng::PlaneBytes bitplanes() const { return m_bound.view; }

    // --- Hooks (layout plano) ---------------------------------------------
    u32 planeline_for(s32 wy) const override {
        // `wy * planes` con `mulu.w` (16x16 -> 32), no `__mulsi3`: es el camino por fila de
        // las primitivas de `Surface` (write_pixel/draw_span) y del relleno de polígono.
        return eng::math::mulu16(static_cast<u16>(wy), m_planes);
    }
    u32 byte_for(s32 wx) const override {
        return static_cast<u32>(wx / 8) & ~1u;
    }
    u32 mirror_planelines() const override { return 0; }
    bool supports_walk() const override { return false; }
    bool in_bounds(s32 wx, s32 wy) const override {
        return wx >= 0 && wy >= 0 && static_cast<u32>(wx) < m_width && static_cast<u32>(wy) < m_height;
    }

    /// Vista de hardware del playfield (base de planos, strides, alto): lo que consume el
    /// backend/Compositor para programar `BPLxPT`/módulos. `CanvasPlayfield` la expone tal cual.
    PlayfieldHardwareView hardware_view() const override {
        PlayfieldHardwareView v;
        v.bitplanes = m_frontbuffer;
        v.real_base = m_frontbuffer; // base del bloque (para BPLxPT)
        v.bitmap_bytes_per_row = m_bytes_per_row;
        v.plane_bytes = m_total_bytes;
        v.planes = m_planes;
        v.bitmap_height = m_height;
        v.display_height = m_height;
        v.display_offset = 0;
        v.split_line = 0;
        v.split_active = false;
        v.viewport_w = m_width;
        v.viewport_h = m_height;
        // Interleaved, fetch ESTÁNDAR (DDFSTRT=$38, 40 B/fila en 320 px): cada
        // scanline interleaved = row_bytes*planes, así BPLMOD = planes*row - row.
        // Nótese que NO lleva el "-2" del corkscrew (DDF $30, fetch 42 B): un
        // canvas plano se muestra con fetch estándar (el compositor lo programa
        // así en su zona overlay); si se mostrara bajo el DDF $30 del corkscrew,
        // el offset de 16 px lo descuadraría (ver XlimitedDisplayComposer).
        v.bpl1mod = static_cast<u16>(eng::math::mulu16(m_bytes_per_row, m_planes) - (m_width / 8u));
        v.bpl2mod = v.bpl1mod;
        return v;
    }

    /// El bitmap que posee este lienzo (memoria + layout).
    const gfx::Bitmap& bitmap() const { return m_bitmap; }
    gfx::Bitmap& bitmap() { return m_bitmap; }

    /// Blit planar en el lienzo (coordenadas de lienzo = fila/columna directas,
    /// sin walk ni costura). `wx` múltiplo de 16, origen en Chip RAM. Fuente con
    /// `Span`: se valida que `src.size()` cubra los `planes` planos solicitados.
    bool add_world_bitmap(graphics::FramePlan& plan, Span<const u16> src,
                          s32 wx, s32 wy, u16 w, u16 h,
                          u16 src_row_bytes, u32 src_plane_stride,
                          u8 planes, u8 source_shift = 0u, bool descending = false,
                          RasterOp op = RasterOp::Copy) override {
        if (!m_initialized || src.empty() || planes == 0) return false;
        if (wx < 0 || (wx & 15) != 0 || static_cast<u32>(wx / 8) + (w / 8u) > m_bytes_per_row) return false;
        if (wy < 0 || static_cast<u32>(wy) + h > m_height) return false;
        const u16 words = static_cast<u16>(w / 16u);
        const u32 need_src = (planes > 1u ? eng::math::mulu16(static_cast<u16>(planes - 1u), static_cast<u16>(src_plane_stride / 2u)) : 0u)
                           + (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u)
                           + static_cast<u32>(words);
        if (src.size() < need_src) return false;
        const bool logic = op != RasterOp::Copy;
        const u16 x_byte = static_cast<u16>(wx / 8u);
        const u32 pl = eng::math::mulu16(static_cast<u16>(wy), m_planes);
        const s16 src_mod = static_cast<s16>(src_row_bytes - words * 2);
        const s16 dst_mod = static_cast<s16>(eng::math::mulu16(m_bytes_per_row, m_planes) - words * 2);
        const u16* sbase = src.data();
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = sbase + eng::math::mulu16(p, static_cast<u16>(src_plane_stride / 2u));
            u16* d = reinterpret_cast<u16*>(m_frontbuffer + eng::math::mulu16(static_cast<u16>(pl + p), m_bytes_per_row) + x_byte);
            graphics::BlitJob job {
                graphics::BlitJobKind::CopyRect, graphics::BlitSource {}, graphics::BlitSource {s}, graphics::BlitDest {d},
                words, h, src_mod, dst_mod,
                1, source_shift, src_plane_stride, eng::math::mulu16(m_bytes_per_row, m_planes), descending
            };
            job.minterm = logic ? raster_op_minterm(op) : job.minterm;
            if (logic ? !plan.add_logic_blit(job) : !plan.add_copy_rect(job)) return false;
        }
        return true;
    }

    /// BOB enmascarado en el lienzo (cookie-cut, máscara 1 bit compartida).
    bool add_world_bitmap_masked(graphics::FramePlan& plan, Span<const u16> src,
                                 Span<const u16> mask, s32 wx, s32 wy,
                                 u16 w, u16 h, u16 src_row_bytes,
                                 u32 src_plane_stride, u8 planes,
                                 u8 source_shift = 0u) override {
        if (!m_initialized || src.empty() || mask.empty() || planes == 0) return false;
        if (wx < 0 || (wx & 15) != 0 || static_cast<u32>(wx / 8) + (w / 8u) > m_bytes_per_row) return false;
        if (wy < 0 || static_cast<u32>(wy) + h > m_height) return false;
        const u16 words = static_cast<u16>(w / 16u);
        // El origen debe cubrir los `planes` planos; la máscara es UN plano de 1
        // bit, así que basta con una viaje de fila (h líneas de `src_row_bytes`).
        const u32 need_src = (planes > 1u ? eng::math::mulu16(static_cast<u16>(planes - 1u), static_cast<u16>(src_plane_stride / 2u)) : 0u)
                           + (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u)
                           + static_cast<u32>(words);
        const u32 need_mask = (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u)
                            + static_cast<u32>(words);
        if (src.size() < need_src || mask.size() < need_mask) return false;
        const u16 x_byte = static_cast<u16>(wx / 8u);
        const u32 pl = eng::math::mulu16(static_cast<u16>(wy), m_planes);
        const s16 src_mod = static_cast<s16>(src_row_bytes - words * 2);
        const s16 dst_mod = static_cast<s16>(eng::math::mulu16(m_bytes_per_row, m_planes) - words * 2);
        const u16* sbase = src.data();
        const u16* mbase = mask.data();
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = sbase + eng::math::mulu16(p, static_cast<u16>(src_plane_stride / 2u));
            u16* d = reinterpret_cast<u16*>(m_frontbuffer + eng::math::mulu16(static_cast<u16>(pl + p), m_bytes_per_row) + x_byte);
            graphics::BlitJob job {
                graphics::BlitJobKind::MaskedBobCookieCut, graphics::BlitSource {mbase}, graphics::BlitSource {s}, graphics::BlitDest {d},
                words, h, src_mod, dst_mod,
                1, source_shift, src_plane_stride, eng::math::mulu16(m_bytes_per_row, m_planes), false
            };
            if (!plan.add_masked_bob(job)) return false;
        }
        return true;
    }

private:
    /// Sincroniza los miembros de la base (mapeo/escritura) desde el Bitmap.
    void sync_from_bitmap() {
        m_width = m_bitmap.width();
        m_height = m_bitmap.height();
        m_planes = m_bitmap.planes();
        m_bytes_per_row = m_bitmap.row_bytes();
        m_total_bytes = m_bitmap.total_bytes();
        m_frontbuffer = m_bitmap.bytes().data(); // vía cruda interna (núcleo)
    }
    gfx::Bitmap m_bitmap {};
    /// Bitplanes externos cuando el lienzo se construyó con `bind` (vacio con `begin`).
    eng::Block<eng::PlaneTag> m_bound {};
};

} // namespace eng::field
