#pragma once

/// \file surface.hpp
/// Contexto de dibujo con recorte (clip) sobre un `Playfield`.
///
/// Según el diseño del engine, `Playfield` NO dibuja: representa el framebuffer
/// hardware + su mapeo lógico→físico. `Surface` es el único contexto de dibujo:
/// una subregión rectangular (origen + tamaño + clip) sobre un playfield, con
/// las primitivas (`set_pixel`, `fill_rect`, `draw_line`, `draw_text`, `blit`,
/// `blit_masked`) recortadas contra su clip y enrutadas por el mapeo del
/// playfield.
///
/// Es la base del GUI: un `Widget` es una `Surface` + `draw()` + `hit_test(punto)`.
///
/// `Surface` es un tipo VALOR (no propietario): se construye sobre la marcha
/// apuntando a un playfield (`Playfield*` no-propietario; `Ref` es un
/// refactor pendiente). La escena expone superficies listas (`bg_surface()`,
/// `hud_surface()`) para el código de juego.
///
/// Regla de API del engine: el programador NO ve punteros a bitplanes, índices
/// de planos, layouts ni registros. `Surface` es el contexto de dispositivo:
/// dibuja igual sobre un playfield EHB, single 4p o DPF, recortado contra su
/// clip, con independencia del modo (el mapeo lo gestiona el `Playfield`).
///
/// ```text
///   Playfield (framebuffer hardware + mapeo lógico→físico)   ← NO dibuja
///   ├─ FlatPlayfield          (chunky / un plano)
///   ├─ MirrorPlayfield        (varios planos espejados)
///   └─ DoubleBufferPlayfield  (2 buffers + swap)
///            ▲ m_target (Ref, no propietario)
///   Surface (origen + tamaño + clip)   ← ÚNICO contexto de dibujo
///     set_pixel · fill_rect · draw_line · draw_text · blit · blit_masked
///            ▲
///   Widget (Surface + draw() + hit_test(punto))
///   → PlayfieldHardwareView (BPLxPT, módulos, alto): lo que consume el backend/Compositor
/// ```

#include <eng/retro/lib2d.hpp>
#include <eng/core/box.hpp>
#include <eng/core/utf8.hpp>
#include <eng/core/ptr.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/raster.hpp>
#include <eng/graphics/font5x7.hpp>
#include <eng/graphics/font8.hpp>

namespace eng::field {

/// Rectángulo en el espacio lógico del playfield (mundo o pantalla).
struct SurfaceRect {
    s32 x = 0;
    s32 y = 0;
    u16 w = 0;
    u16 h = 0;
};

/// Conversión `eng::Box` → `SurfaceRect` (el rect lógico de 32 bits que usa `Surface`).
[[nodiscard]] constexpr SurfaceRect surface_rect_of(const eng::Box& b) {
    return { b.x, b.y, b.w, b.h };
}

/// Conversión `SurfaceRect` → `eng::Box` (acota a 16 bits: en pantalla no desborda).
[[nodiscard]] constexpr eng::Box box_of(const SurfaceRect& r) {
    return { static_cast<eng::s16>(r.x), static_cast<eng::s16>(r.y), r.w, r.h };
}

/// Contexto de dibujo con clip sobre un playfield.
class Surface {
public:
    Surface() = default;
    Surface(Playfield& target, const SurfaceRect& clip)
        : m_target(target), m_clip(clip) {}

    constexpr bool valid() const { return m_target.valid(); }
    constexpr eng::Ref<const Playfield> target() const { return m_target; }
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

    /// Rectángulo relleno (CPU o Blitter según el `Rasterizer` del playfield), recortado,
    /// con la operación lógica `op` (`Copy` por defecto). true si todo el rect estaba dentro.
    bool fill_rect(s32 x, s32 y, u16 w, u16 h, u8 color, RasterOp op = RasterOp::Copy) {
        if (!valid()) return false;
        if (w == 0u || h == 0u) return true;
        const s32 x1 = x + static_cast<s32>(w) - 1;
        const s32 y1 = y + static_cast<s32>(h) - 1;
        const s32 cx0 = m_clip.x;
        const s32 cy0 = m_clip.y;
        const s32 cx1 = m_clip.x + static_cast<s32>(m_clip.w) - 1;
        const s32 cy1 = m_clip.y + static_cast<s32>(m_clip.h) - 1;
        const bool fully_inside = x >= cx0 && y >= cy0 && x1 <= cx1 && y1 <= cy1;
        const s32 rx0 = x < cx0 ? cx0 : x;
        const s32 rx1 = x1 > cx1 ? cx1 : x1;
        const s32 ry0 = y < cy0 ? cy0 : y;
        const s32 ry1 = y1 > cy1 ? cy1 : y1;
        if (rx1 < rx0 || ry1 < ry0) return false; // rect fuera del clip
        rasterizer()->fill_rect(*m_target.get(), rx0, ry0,
                                static_cast<u16>(rx1 - rx0 + 1),
                                static_cast<u16>(ry1 - ry0 + 1), color, op);
        return fully_inside;
    }

    /// Polígono **convexo** relleno, recortado contra el clip de la superficie.
    ///
    /// Recorta el polígono con `eng::retro::clip_polygon` (Sutherland-Hodgman) contra el
    /// clip y **delega** en el playfield (`Playfield::fill_polygon`): CPU scanline
    /// por defecto, o **Blitter** en un playfield de Amiga (hook del backend; ver el
    /// truco `BLTDPTR` de la demo 116). Así el motor de relleno es del backend y el
    /// llamador solo pide «pinta esta cara» igual en EHB/single/DPF.
    bool fill_polygon(const s16* xs, const s16* ys, u8 n, u8 color) {
        if (!valid() || xs == nullptr || ys == nullptr || n < 3u) return false;
        constexpr u8 kCap = 12; // convexo y recortado: nunca crece más de 4 lados
        eng::retro::Vec2 in[kCap];
        eng::retro::Vec2 tmp[kCap];
        const u8 nn = n < kCap ? n : kCap;
        for (u8 i = 0u; i < nn; ++i) {
            in[i] = eng::retro::v2(xs[i], ys[i]);
        }
        const eng::retro::Rect win = eng::retro::rect(
            static_cast<s16>(m_clip.x), static_cast<s16>(m_clip.y),
            static_cast<s16>(m_clip.x + static_cast<s32>(m_clip.w) - 1),
            static_cast<s16>(m_clip.y + static_cast<s32>(m_clip.h) - 1));
        const u32 m = eng::retro::clip_polygon(win, in, tmp, nn,
                                           static_cast<u8>(eng::retro::PF_LEFT | eng::retro::PF_TOP |
                                                           eng::retro::PF_RIGHT | eng::retro::PF_BOTTOM));
        if (m < 3u) return true;
        s16 cx[kCap];
        s16 cy[kCap];
        for (u32 i = 0u; i < m; ++i) {
            cx[i] = in[i].x().v;
            cy[i] = in[i].y().v;
        }
        return m_target->fill_polygon(cx, cy, static_cast<u8>(m), color);
    }

    /// Línea, recortada al clip de la superficie. Delegada en el `Rasterizer` (CPU por
    /// defecto; un rasterizador Blitter la traza por hardware si se pasa `plan`;
    /// `op == Xor` usa la variante EOR/ONEDOT).
    bool draw_line(s32 x0, s32 y0, s32 x1, s32 y1, u8 color,
                   eng::Ref<graphics::FramePlan> plan = {}, RasterOp op = RasterOp::Copy) {
        if (!valid()) return false;
        const ClipRect clip {m_clip.x, m_clip.y,
                             m_clip.x + static_cast<s32>(m_clip.w) - 1,
                             m_clip.y + static_cast<s32>(m_clip.h) - 1};
        return rasterizer()->draw_line(*m_target.get(), clip, x0, y0, x1, y1, color, plan, op);
    }

    /// Texto en una fuente 8×8, a nivel de contexto (sin punteros ni planos).
    ///
    /// Escribe la cadena `text` (UTF-8) empezando en el píxel `(x, y)` (esquina
    /// superior izquierda del primer glifo), con el color de playfield `color`
    /// (índice en la paleta del backend). Cada glifo ocupa 8x8 píxeles; los
    /// glifos fuera del clip se recortan. Devuelve `false` si el contexto no es
    /// válido; una cadena vacía es válida (no pinta nada) y devuelve `true`.
    ///
    /// El texto se decodifica UTF-8 (ver `eng/core/utf8.hpp`) y la fuente cubre
    /// ASCII + LATIN-1 (acentos, diéresis, ñ/Ñ…); los puntos fuera de ese
    /// subconjunto no se pintan. La fuente se enruta por `set_pixel`, así que
    /// funciona igual sobre un playfield EHB, single o dual playfield: el mapeo
    /// y los colores dependen del `Playfield`, no de esta llamada.
    /// Cadenas RUNTIME: decodifica UTF-8 (1-2 bytes por code point). Para
    /// literales, usar `draw_text_literal` (compile-time). La fuente cubre
    /// ASCII + LATIN-1 (acentos, diéresis, ñ/Ñ…); lo demás no se pinta.
    bool draw_text(s32 x, s32 y, const char* text, u8 color) {
        if (!valid()) return false;
        if (text == nullptr) return true;
        const u8* p = reinterpret_cast<const u8*>(text);
        for (;;) {
            const u32 cp = eng::utf8::decode(p);
            // decode() devuelve 0 tanto al final de cadena (NUL) como ante un
            // byte inválido/fuera de LATIN-1; en ambos casos hay que cortar,
            // porque seguir leería la .rodata posterior a la cadena y rasterizaría
            // basura (doble texto solapado con el color de la llamada anterior).
            if (cp == 0u) {
                break;
            }
            if (cp >= 32u) {
                draw_code_point(x, y, cp, color);
            }
            x += 8;
        }
        return true;
    }

    /// Texto a partir de un literal de cadena, decodificado EN COMPILE-TIME.
    ///
    /// Uso: `surf.draw_text_literal<"Você">(x, y, color);` — el literal viaja
    /// como NTTP (`eng::utf8::fixed_string`), se decodifica en compile-time y en
    /// runtime solo se pintan los code points (cero procesamiento de cadena).
    template <eng::utf8::fixed_string Str>
    bool draw_text_literal(s32 x, s32 y, u8 color) {
        if (!valid()) return false;
        for (eng::usize i = 0; i < eng::utf8::lit<Str>::count; ++i) {
            const u32 cp = eng::utf8::lit<Str>::cp[i];
            if (cp >= 32u) {
                draw_code_point(x, y, cp, color);
            }
            x += 8;
        }
        return true;
    }

    /// Texto desde code points ya decodificados (sin volver a decodificar).
    bool draw_codepoints(s32 x, s32 y, const u32* cps, eng::usize count, u8 color) {
        if (!valid() || cps == nullptr) return false;
        for (eng::usize i = 0; i < count; ++i) {
            if (cps[i] >= 32u) {
                draw_code_point(x, y, cps[i], color);
            }
            x += 8;
        }
        return true;
    }

    /// Texto compacto en la fuente 5x7 (HUD), a nivel de contexto.
    ///
    /// Igual que `draw_text` pero con la fuente compacta `Font5x7` (7 filas, 5
    /// columnas, bit4=izquierda). Útil para HUDs y texto denso. Acepta UTF-8 y
    /// cubre las mayúsculas LATIN-1 (acentos, diéresis, Ñ/Ç).
    bool draw_text5(s32 x, s32 y, const char* text, u8 color) {
        if (!valid()) return false;
        if (text == nullptr) return true;
        const u8* p = reinterpret_cast<const u8*>(text);
        for (;;) {
            const u32 cp = eng::utf8::decode(p);
            if (cp == 0) {
                break;
            }
            if (static_cast<u16>(cp) < 32u) {
                continue;
            }
            for (u8 r = 0; r < eng::Font5x7::kRows; ++r) {
                const u8 glyph_row = eng::Font5x7::row(static_cast<u16>(cp), r);
                if (glyph_row != 0) {
                    draw_glyph_row(x, y + static_cast<s32>(r), glyph_row, 5u, true, color);
                }
            }
            x += 5; // avance de 5 px (sin espacio extra entre glifos)
        }
        return true;
    }

    /// Blit planar en el mundo (delega en el `Rasterizer`: CPU o Blitter). Recorta el
    /// rect contra el clip. `source_shift` (shifts A/B) y `descending` (blits solapados)
    /// solo aplican al camino Blitter. `op` (`Or`/`And`/`Xor`) aplica la operación lógica
    /// (sombras/glow/máscaras). La fuente viaja como `Span`.
    bool blit(graphics::FramePlan& plan, Span<const u16> src, s32 x, s32 y,
              u16 w, u16 h, u16 src_row_bytes, u32 src_plane_stride, u8 planes,
              u8 source_shift = 0u, bool descending = false, RasterOp op = RasterOp::Copy) {
        if (!valid() || x < m_clip.x || y < m_clip.y ||
            x + static_cast<s32>(w) > m_clip.x + m_clip.w ||
            y + static_cast<s32>(h) > m_clip.y + m_clip.h) return false;
        return rasterizer()->copy_rect(*m_target.get(), plan, src, x, y, w, h,
                                       src_row_bytes, src_plane_stride, planes,
                                       source_shift, descending, op);
    }

    /// **Sombra** (oscurece donde la máscara de `src` está a 1): `blit` con `RasterOp::And`.
    /// Requiere rasterizador Blitter (la CPU no aplica la operación lógica).
    bool blit_shadow(graphics::FramePlan& plan, Span<const u16> src, s32 x, s32 y,
                     u16 w, u16 h, u16 src_row_bytes, u32 src_plane_stride, u8 planes) {
        return blit(plan, src, x, y, w, h, src_row_bytes, src_plane_stride, planes, 0u, false,
                    RasterOp::And);
    }

    /// **Glow/aditivo** (ilumina donde la máscara de `src` está a 1): `blit` con `RasterOp::Or`.
    /// Requiere rasterizador Blitter (la CPU no aplica la operación lógica).
    bool blit_glow(graphics::FramePlan& plan, Span<const u16> src, s32 x, s32 y,
                   u16 w, u16 h, u16 src_row_bytes, u32 src_plane_stride, u8 planes) {
        return blit(plan, src, x, y, w, h, src_row_bytes, src_plane_stride, planes, 0u, false,
                    RasterOp::Or);
    }

    /// BOB enmascarado (cookie-cut) en el mundo, recortado contra el clip.
    bool blit_masked(graphics::FramePlan& plan, Span<const u16> src, Span<const u16> mask,
                     s32 x, s32 y, u16 w, u16 h,
                     u16 src_row_bytes, u32 src_plane_stride, u8 planes,
                     u8 source_shift = 0u) {
        if (!valid() || x < m_clip.x || y < m_clip.y ||
            x + static_cast<s32>(w) > m_clip.x + m_clip.w ||
            y + static_cast<s32>(h) > m_clip.y + m_clip.h) return false;
        return rasterizer()->copy_masked(*m_target.get(), plan, src, mask, x, y, w, h,
                                         src_row_bytes, src_plane_stride, planes, source_shift);
    }

private:
    /// Rasterizador efectivo: el del playfield o el CPU por defecto (nunca nulo).
    eng::NonNull<Rasterizer> rasterizer() const {
        const eng::Ref<Rasterizer> r = m_target->rasterizer();
        return r.valid() ? eng::NonNull<Rasterizer>(*r) : eng::NonNull<Rasterizer>(kCpuRaster);
    }
    /// Pinta los bits ACTIVOS de una fila de glifo como tramos horizontales (una palabra por
    /// plano, `Playfield::draw_span`). `msb_first` = el bit `nbits-1` es la columna 0 (fuente 5x7).
    void draw_glyph_row(s32 x, s32 y, u8 bits, u8 nbits, bool msb_first, u8 color) {
        const s32 cx0 = m_clip.x;
        const s32 cx1 = m_clip.x + static_cast<s32>(m_clip.w) - 1;
        if (y < m_clip.y || y >= m_clip.y + static_cast<s32>(m_clip.h)) return;
        auto is_set = [&](u8 c) {
            return ((bits >> (msb_first ? (nbits - 1u - c) : c)) & 1u) != 0u;
        };
        u8 k = 0;
        while (k < nbits) {
            if (!is_set(k)) {
                ++k;
                continue;
            }
            u8 j = k;
            while (j + 1u < nbits && is_set(static_cast<u8>(j + 1u))) {
                ++j;
            }
            s32 a = x + static_cast<s32>(k);
            s32 b = x + static_cast<s32>(j);
            if (a < cx0) a = cx0;
            if (b > cx1) b = cx1;
            if (b >= a) {
                m_target->draw_span(a, b, y, color);
            }
            k = static_cast<u8>(j + 1u);
        }
    }

    /// Pinta un code point con la fuente 8x8 (por tramos, `draw_glyph_row`).
    void draw_code_point(s32 x, s32 y, u32 cp, u8 color) {
        for (u8 row = 0; row < eng::Font8::kRows; ++row) {
            const u8 glyph_row = eng::Font8::row(static_cast<u16>(cp), row);
            if (glyph_row != 0) {
                draw_glyph_row(x, y + static_cast<s32>(row), glyph_row, 8u, false, color);
            }
        }
    }

    eng::Ref<Playfield> m_target {}; // no-propietario (Ref, sin puntero crudo)
    SurfaceRect m_clip {};
};

} // namespace eng::field
