#pragma once

/// \file contiguous_playfield.hpp
/// Lienzo planar **contiguo** (planos uno tras otro): el layout de las escenas
/// EHB/HAM. Definido aparte de `playfield_base.hpp`; `playfield.hpp` es la cabecera
/// de familia que reúne base e implementaciones.

#include <eng/field/playfield_base.hpp>

namespace eng::field {

/// Lienzo planar **contiguo**: los planos van uno tras otro (`plano p` en
/// `base + p*plane_bytes`), el layout que usan las escenas EHB/HAM del modelo de
/// composición. Implementa el mismo contrato que `CanvasPlayfield`, de modo que
/// `Surface` (`set_pixel`/`draw_line`/`fill_rect`/`fill_polygon`/`draw_text`) dibuja
/// igual sobre contiguo o interleaved sin que la app vea planos, punteros ni layouts.
///
/// No posee memoria: el `Scene` le pasa sus buffers con `bind`. Los blits
/// (`add_world_bitmap*`) no están implementados para este layout (sin consumidor).
class ContiguousPlayfield : public Playfield {
public:
    /// Construye el lienzo sobre bitplanes YA reservados. `bitplanes.view.size()` debe
    /// cubrir `plane_stride * planes`. `plane_stride` es el tamaño de un plano completo
    /// (`row_bytes * filas_lógicas`); 0 = derivarlo de `height`.
    bool bind(eng::Block<eng::PlaneTag> bitplanes, u16 width, u16 height, u8 planes,
              u32 plane_stride = 0u) {
        if (!bitplanes.valid()) return false;
        if (!bind_raw(bitplanes.view.data(), static_cast<u32>(bitplanes.view.size()),
                      width, height, planes, plane_stride)) {
            return false;
        }
        m_bound = bitplanes;
        return true;
    }

    /// Igual que `bind` pero sobre memoria **cruda** (base + bytes): permite enlazar
    /// buffers con otro tag de dominio (p. ej. una máscara de 1 bit) sin acoplar el
    /// playfield a ese tag. El llamador garantiza que la memoria es Chip y del tamaño.
    bool bind_raw(eng::u8* base, u32 bytes, u16 width, u16 height, u8 planes,
                  u32 plane_stride = 0u) {
        if (base == nullptr || width == 0u || height == 0u || planes == 0u || planes > 6u) {
            return false;
        }
        const u16 row = static_cast<u16>(((width / 8u) + 3u) & ~3u);
        const u32 pbytes = (plane_stride != 0u)
                               ? plane_stride
                               : eng::math::mulu32x16(static_cast<u32>(row), height);
        const u32 need = eng::math::mulu32x16(pbytes, static_cast<u16>(planes));
        if (bytes < need) return false;
        m_bound = {};
        m_width = width;
        m_height = height;
        m_planes = planes;
        m_bytes_per_row = row;
        m_total_bytes = need;
        m_frontbuffer = base; // vía cruda interna (núcleo)
        m_plane_stride = pbytes;
        m_row_stride = row;
        m_initialized = true;
        return true;
    }

    /// Planos del lienzo (vista de dominio; vacía si se enlazó con `bind_raw`).
    [[nodiscard]] constexpr eng::PlaneBytes bitplanes() const {
        return m_bound.valid() ? m_bound.view
                               : eng::PlaneBytes {m_frontbuffer, m_total_bytes};
    }

    // --- Hooks (layout contiguo) ------------------------------------------
    u32 planeline_for(s32 wy) const override { return static_cast<u32>(wy); }
    u32 byte_for(s32 wx) const override { return static_cast<u32>(wx / 8) & ~1u; }
    bool in_bounds(s32 wx, s32 wy) const override {
        return wx >= 0 && wy >= 0 && static_cast<u32>(wx) < m_width &&
               static_cast<u32>(wy) < m_height;
    }
    // Strides que ve el sink de relleno (Blitter): planos contiguos.
    u32 plane_stride() const override { return m_plane_stride; }
    u32 row_stride() const override { return m_row_stride; }

    /// Vista de hardware para el compositor de la escena (BPLxPT, módulos, alto):
    /// planos contiguos con módulos a 0.
    PlayfieldHardwareView hardware_view() const override {
        PlayfieldHardwareView v;
        v.bitplanes = m_frontbuffer;
        v.real_base = m_frontbuffer;
        v.bitmap_bytes_per_row = m_bytes_per_row;
        v.plane_bytes = m_plane_stride;
        v.planes = m_planes;
        v.bitmap_height = m_height;
        v.display_height = m_height;
        v.bpl1mod = 0; // planos contiguos: sin módulo entre filas de un plano
        v.bpl2mod = 0;
        v.viewport_w = m_width;
        v.viewport_h = m_height;
        return v;
    }

    /// Blit planar (copia rectangular) sobre lienzo **contiguo**: encola un `CopyRect`
    /// por plano, con `destination_plane_stride = plane_bytes` y módulo de fila = una
    /// fila de un plano. `wx` debe ser múltiplo de 16.
    bool add_world_bitmap(graphics::FramePlan& plan, Span<const u16> src,
                          s32 wx, s32 wy, u16 w, u16 h,
                          u16 src_row_bytes, u32 src_plane_stride,
                          u8 planes, u8 source_shift = 0u, bool descending = false,
                          RasterOp op = RasterOp::Copy) override {
        if (!m_initialized || src.empty() || planes == 0u) return false;
        if (wx < 0 || (wx & 15) != 0 || static_cast<u32>(wx / 8) + (w / 8u) > m_bytes_per_row) return false;
        if (wy < 0 || static_cast<u32>(wy) + h > m_height) return false;
        const u16 words = static_cast<u16>(w / 16u);
        const u32 need_src = (planes > 1u ? eng::math::mulu16(static_cast<u16>(planes - 1u), static_cast<u16>(src_plane_stride / 2u)) : 0u)
                           + (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u)
                           + static_cast<u32>(words);
        if (src.size() < need_src) return false;
        const bool logic = op != RasterOp::Copy;
        const u16 x_byte = static_cast<u16>(wx / 8u);
        const s16 src_mod = static_cast<s16>(src_row_bytes - words * 2);
        const s16 dst_mod = static_cast<s16>(m_bytes_per_row - words * 2);
        const u16* sbase = src.data();
        const u32 y0_off = eng::math::mulu16(static_cast<u16>(wy), static_cast<u16>(m_row_stride));
        const u8* sp = reinterpret_cast<const u8*>(sbase);
        u8* dp = m_frontbuffer + y0_off + x_byte;
        // Fuente con su propio ancho de fila (`source_words_per_row`): el blit puede usar este
        // playfield como fuente (p. ej. `copy_rect_from`) o leer un bitmap con padding propio.
        const u16 src_wpr = static_cast<u16>(src_row_bytes / 2u);
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = reinterpret_cast<const u16*>(sp);
            u16* d = reinterpret_cast<u16*>(dp);
            graphics::BlitJob job {
                graphics::BlitJobKind::CopyRect, graphics::BlitSource {}, graphics::BlitSource {s}, graphics::BlitDest {d},
                words, h, src_mod, dst_mod,
                1, source_shift, src_plane_stride, m_plane_stride, descending
            };
            job.source_words_per_row = src_wpr;
            job.minterm = logic ? raster_op_minterm(op) : job.minterm;
            if (logic ? !plan.add_logic_blit(job) : !plan.add_copy_rect(job)) return false;
            sp += src_plane_stride;
            dp += m_plane_stride;
        }
        return true;
    }

    /// BOB enmascarado (cookie-cut) sobre lienzo **contiguo**: un `MaskedBobCookieCut`
    /// por plano, con la máscara de 1 bit compartida.
    bool add_world_bitmap_masked(graphics::FramePlan& plan, Span<const u16> src,
                                 Span<const u16> mask, s32 wx, s32 wy,
                                 u16 w, u16 h, u16 src_row_bytes,
                                 u32 src_plane_stride, u8 planes,
                                 u8 source_shift = 0u) override {
        if (!m_initialized || src.empty() || mask.empty() || planes == 0u) return false;
        if (wx < 0 || (wx & 15) != 0 || static_cast<u32>(wx / 8) + (w / 8u) > m_bytes_per_row) return false;
        if (wy < 0 || static_cast<u32>(wy) + h > m_height) return false;
        const u16 words = static_cast<u16>(w / 16u);
        const u32 need_src = (planes > 1u ? eng::math::mulu16(static_cast<u16>(planes - 1u), static_cast<u16>(src_plane_stride / 2u)) : 0u)
                           + (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u)
                           + static_cast<u32>(words);
        const u32 need_mask = (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u)
                            + static_cast<u32>(words);
        if (src.size() < need_src || mask.size() < need_mask) return false;
        const u16 x_byte = static_cast<u16>(wx / 8u);
        const s16 src_mod = static_cast<s16>(src_row_bytes - words * 2);
        const s16 dst_mod = static_cast<s16>(m_bytes_per_row - words * 2);
        const u16* sbase = src.data();
        const u16* mbase = mask.data();
        const u32 y0_off = eng::math::mulu16(static_cast<u16>(wy), static_cast<u16>(m_row_stride));
        const u8* sp = reinterpret_cast<const u8*>(sbase);
        u8* dp = m_frontbuffer + y0_off + x_byte;
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = reinterpret_cast<const u16*>(sp);
            u16* d = reinterpret_cast<u16*>(dp);
            graphics::BlitJob job {
                graphics::BlitJobKind::MaskedBobCookieCut, graphics::BlitSource {mbase}, graphics::BlitSource {s}, graphics::BlitDest {d},
                words, h, src_mod, dst_mod,
                1, source_shift, src_plane_stride, m_plane_stride, false
            };
            if (!plan.add_masked_bob(job)) return false;
            sp += src_plane_stride;
            dp += m_plane_stride;
        }
        return true;
    }

private:
    eng::Block<eng::PlaneTag> m_bound {}; ///< bitplanes externos (sin propiedad)
};

} // namespace eng::field
