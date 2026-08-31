#pragma once

/// \file scroll_engine.hpp
/// Driver + algoritmo del scroll separados del playfield (diseño §7 `ScrollEngine`).
///
/// `ScrollEngine` posee el ESTADO de cámara (mappos/videopos/dirección), decide
/// el paso de 1 px a partir de `(dx, dy)` y EMITE los blits de los 4 scrollear
/// del corkscrew/XYLimited (Steger). El playfield es el **layout sink**
/// (`ScrollSink`, concepto sin virtuals, resuelto en compile-time): aporta la
/// geometría, los límites del mapa, `add_draw` (blit de bloque + espejo) y la
/// costura de la guarda de 1 word (`save_word`/`restore_saveword`).
///
/// La guarda saveword es un concepto del LAYOUT (el blit plane-shifted pisa y
/// restaura una word del framebuffer de ese layout concreto), así que vive en
/// el sink, no en el engine: el engine sólo decide CUÁNDO restaurar según su
/// `previous_xdirection`. El estado del engine queda en cámara + dirección.
///
/// Reglas del engine: sin heap, sin RTTI, sin virtuals en el hot path (sink por
/// template), gnu++23.

#include <eng/core/types.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::field {

/// Direcciones del corkscrew (constantes del original Scroller_XYLimited).
enum ScrollDirection : u8 {
    ScrollDirNone = 0,  // primer paso, sin dirección previa
    ScrollDirLeft = 1,
    ScrollDirRight = 2,
};

/// Estado de cámara del scroll. Lo mueve `ScrollEngine` (paso a paso) y lo lee
/// el display (`hardware_view`/getters). La guarda saveword NO está aquí: es un
/// seam de layout que gestiona el sink.
struct ScrollState {
    s32 mapposx = 0;
    s32 videoposx = 0;
    s32 mapposy = 0;
    s32 videoposy = 0;
    u8 previous_xdirection = ScrollDirNone;
};

/// Concepto `ScrollSink` (por convención, sin SFINAE). El playfield que ejecuta
/// el scroll debe exponer (const, u16 a menos que se indique):
///   tile_width, tile_height, planes(u8), viewport_w, viewport_h,
///   display_height, display_planelines, bitmap_blocks_per_row,
///   bitmap_blocks_per_col, block_planes_lines, bytes_per_row, bitmap_width,
///   map_width_blocks, map_height_blocks, map_wrap_x, map_wrap_y,
///   bool one_direction(),
///   bool add_draw(FramePlan&, u16 x, u16 y, u16 mapx, u16 mapy),
///   void save_word(u32 byte_offset), void restore_saveword().
/// Un template usa estos nombres; un fallo de conformidad es un error de
/// compilación claro (no un UB).

/// Driver del scroll: estado + algoritmo de los 4 movimientos Amtplo.
template <class Sink>
class ScrollEngine {
public:
    ScrollState& state() { return m_state; }
    const ScrollState& state() const { return m_state; }

    /// Avanza 1 px por eje (0 si no toca). Réplica del driver original: primero
    /// X y luego Y en la misma llamada (diagonal posible). Devuelve false si un
    /// borde del mapa bloqueó el avance en algún eje.
    bool step(graphics::FramePlan& plan, Sink& sink, s32 dx, s32 dy) {
        if (dx != 0 && !((dx > 0) ? scroll_right(plan, sink) : scroll_left(plan, sink))) return false;
        if (dy != 0 && !((dy > 0) ? scroll_down(plan, sink) : scroll_up(plan, sink))) return false;
        return true;
    }

    /// Fila de bloque del mapa que el display está mostrando (0..display_height).
    inline u16 block_videoposy(const Sink& sn) const {
        return static_cast<u16>(
            (m_state.mapposy / sn.tile_height() * sn.tile_height()) % sn.display_height());
    }
    /// Constante de "dos bloques" del corkscrew (fillup a dos bloques de ancho).
    inline u16 twoblockstep(const Sink& sn) const {
        return sn.bitmap_blocks_per_row() > sn.tile_height()
            ? static_cast<u16>(sn.bitmap_blocks_per_row() - sn.tile_height()) : 0;
    }

    /// Scroll de 1 px a la derecha (plane-shifted) — ScrollRight corkscrew.
    /// Fiel a ScrollRight de Scroller_XYLimited/main.c:869-978:
    ///   columna entrante x = BITMAPWIDTH + ROUND2BLOCKWIDTH(videoposx),
    ///   fila mapy = stepx+1 (2 bloques si stepx==0), y = (block_videoposy +
    ///   mapy*TH) % display_height, ajuste de la fila de fillup al completar.
    bool scroll_right(graphics::FramePlan& plan, Sink& sn) {
        const s32 limit = static_cast<s32>(sn.map_width_blocks()) * sn.tile_width() -
                          sn.viewport_w() - sn.tile_width();
        if (sn.map_wrap_x() == 0 && m_state.mapposx >= limit) return false;

        const u16 mapblockx = static_cast<u16>(m_state.mapposx / sn.tile_width());
        const u16 mapblocky = static_cast<u16>(m_state.mapposy / sn.tile_height());
        const u16 stepx = static_cast<u16>(m_state.mapposx & (sn.tile_width() - 1));
        const u16 stepy = static_cast<u16>(m_state.mapposy & (sn.tile_height() - 1));
        const u16 bvpos = block_videoposy(sn);
        const u16 x0 = static_cast<u16>(m_state.videoposx & ~(sn.tile_width() - 1));
        const u16 mapx = static_cast<u16>(mapblockx + sn.bitmap_blocks_per_row());

        if (!sn.one_direction() && m_state.previous_xdirection == ScrollDirLeft) sn.restore_saveword();

        u16 mapy = static_cast<u16>(stepx + 1);
        if (mapy == 1) { // stepx == 0 → dos bloques
            mapy = static_cast<u16>(mapy + mapblocky);
            const u16 y = static_cast<u16>(((bvpos + sn.tile_height()) % sn.display_height()) * sn.planes());
            if (!sn.add_draw(plan, static_cast<u16>(x0 + sn.bitmap_width()), y, mapx, mapy)) return false;
            const u16 y2 = static_cast<u16>((y + sn.block_planes_lines()) % sn.display_planelines());
            sn.save_word(static_cast<u32>(y2 + sn.block_planes_lines() - 1) * sn.bytes_per_row() +
                         ((x0 + sn.bitmap_width()) / 8u));
            if (!sn.add_draw(plan, static_cast<u16>(x0 + sn.bitmap_width()), y2, mapx, static_cast<u16>(mapy + 1))) return false;
        } else { // un bloque
            ++mapy;
            const u16 y = static_cast<u16>(((bvpos + mapy * sn.tile_height()) % sn.display_height()) * sn.planes());
            mapy = static_cast<u16>(mapy + mapblocky);
            sn.save_word(static_cast<u32>(y + sn.block_planes_lines() - 1) * sn.bytes_per_row() +
                         ((x0 + sn.bitmap_width()) / 8u));
            if (!sn.add_draw(plan, static_cast<u16>(x0 + sn.bitmap_width()), y, mapx, mapy)) return false;
        }

        ++m_state.mapposx;
        m_state.videoposx = m_state.mapposx;
        const u16 new_stepx = static_cast<u16>(m_state.mapposx & (sn.tile_width() - 1));

        if (new_stepx == 0) {
            // Columna completada: ajustar la fila de fillup (valores POST-incremento).
            const u16 nx0 = static_cast<u16>(x0 + sn.tile_width());
            const u16 nmapblockx = static_cast<u16>(mapblockx + 1);
            if (!sn.add_draw(plan,
                    static_cast<u16>(nx0 + (sn.bitmap_blocks_per_row() - 1) * sn.tile_width()),
                    static_cast<u16>(bvpos * sn.planes()),
                    static_cast<u16>(nmapblockx + sn.bitmap_blocks_per_row() - 1), mapblocky)) return false;
            if (stepy) {
                const u16 mx = stepy >= twoblockstep(sn)
                    ? static_cast<u16>(stepy + (twoblockstep(sn) - 1))
                    : static_cast<u16>(stepy * 2 - 1);
                if (!sn.add_draw(plan,
                        static_cast<u16>(nx0 + mx * sn.tile_width()),
                        static_cast<u16>(bvpos * sn.planes()),
                        static_cast<u16>(mx + nmapblockx),
                        static_cast<u16>(mapblocky + sn.bitmap_blocks_per_col()))) return false;
            }
        }

        m_state.previous_xdirection = new_stepx ? ScrollDirRight : ScrollDirNone;
        return true;
    }

    /// Scroll de 1 px a la izquierda (no plane-shifted) — ScrollLeft corkscrew.
    /// Fiel a ScrollLeft de Scroller_XYLimited/main.c:751-867.
    bool scroll_left(graphics::FramePlan& plan, Sink& sn) {
        if (m_state.mapposx < 1) return false;
        --m_state.mapposx;
        m_state.videoposx = m_state.mapposx;

        const u16 mapblockx = static_cast<u16>(m_state.mapposx / sn.tile_width());
        const u16 mapblocky = static_cast<u16>(m_state.mapposy / sn.tile_height());
        const u16 stepx = static_cast<u16>(m_state.mapposx & (sn.tile_width() - 1));
        const u16 stepy = static_cast<u16>(m_state.mapposy & (sn.tile_height() - 1));
        const u16 bvpos = block_videoposy(sn);
        const u16 x0 = static_cast<u16>(m_state.videoposx & ~(sn.tile_width() - 1));

        if (stepx == static_cast<u16>(sn.tile_width() - 1)) {
            // Columna completada: ajustar la fila de fillup.
            u16 mapx = mapblockx;
            u16 mapy = mapblocky;
            if (stepy) mapy = static_cast<u16>(mapy + sn.bitmap_blocks_per_col());
            if (!sn.add_draw(plan, x0, static_cast<u16>(bvpos * sn.planes()), mapx, mapy)) return false;
            mapx = stepy;
            if (mapx) {
                mapx = mapx >= twoblockstep(sn)
                    ? static_cast<u16>(mapx + twoblockstep(sn))
                    : static_cast<u16>(mapx * 2);
                if (!sn.add_draw(plan,
                        static_cast<u16>(x0 + mapx * sn.tile_width()),
                        static_cast<u16>(bvpos * sn.planes()),
                        static_cast<u16>(mapx + mapblockx),
                        static_cast<u16>(mapy - sn.bitmap_blocks_per_col()))) return false;
            }
        }

        const u16 mapx = mapblockx;
        u16 mapy = static_cast<u16>(stepx + 1);
        if (m_state.previous_xdirection == ScrollDirRight) sn.restore_saveword();
        if (mapy == 1) { // stepx == 0 → dos bloques
            mapy = static_cast<u16>(mapy + mapblocky);
            const u16 y = static_cast<u16>(((bvpos + sn.tile_height()) % sn.display_height()) * sn.planes());
            sn.save_word(static_cast<u32>(y) * sn.bytes_per_row() + (x0 / 8u));
            if (!sn.add_draw(plan, x0, y, mapx, mapy)) return false;
            const u16 y2 = static_cast<u16>((y + sn.block_planes_lines()) % sn.display_planelines());
            if (!sn.add_draw(plan, x0, y2, mapx, static_cast<u16>(mapy + 1))) return false;
        } else { // un bloque
            ++mapy;
            const u16 y = static_cast<u16>(((bvpos + mapy * sn.tile_height()) % sn.display_height()) * sn.planes());
            mapy = static_cast<u16>(mapy + mapblocky);
            sn.save_word(static_cast<u32>(y) * sn.bytes_per_row() + (x0 / 8u));
            if (!sn.add_draw(plan, x0, y, mapx, mapy)) return false;
        }

        m_state.previous_xdirection = stepx ? ScrollDirLeft : ScrollDirNone;
        return true;
    }

    /// Scroll vertical 1 px hacia abajo — ScrollDown corkscrew.
    /// Fiel a ScrollDown de Scroller_XYLimited/main.c:639-749.
    bool scroll_down(graphics::FramePlan& plan, Sink& sn) {
        const s32 limitY = static_cast<s32>(sn.map_height_blocks()) * sn.tile_height() -
                           sn.viewport_h() - sn.tile_height();
        if (sn.map_wrap_y() == 0 && m_state.mapposy >= limitY) return false;

        const u16 mapblockx = static_cast<u16>(m_state.mapposx / sn.tile_width());
        const u16 mapblocky = static_cast<u16>(m_state.mapposy / sn.tile_height());
        const u16 stepx = static_cast<u16>(m_state.mapposx & (sn.tile_width() - 1));
        const u16 stepy = static_cast<u16>(m_state.mapposy & (sn.tile_height() - 1));
        const u16 bvpos = block_videoposy(sn);
        const u16 x0 = static_cast<u16>(m_state.videoposx & ~(sn.tile_width() - 1));
        const u16 y_pl = static_cast<u16>(bvpos * sn.planes());
        const u16 mapy = static_cast<u16>(mapblocky + sn.bitmap_blocks_per_col());

        // Fila entrante en la banda de staging (valores PRE-incremento).
        if (stepy >= twoblockstep(sn)) {
            const u16 mx = static_cast<u16>(stepy + twoblockstep(sn) + mapblockx);
            const u16 x = static_cast<u16>((stepy + twoblockstep(sn)) * sn.tile_width() + x0);
            if (!sn.add_draw(plan, x, y_pl, mx, mapy)) return false;
        } else {
            const u16 mx = static_cast<u16>(stepy * 2 + mapblockx);
            const u16 x = static_cast<u16>(stepy * 2 * sn.tile_width() + x0);
            if (!sn.add_draw(plan, x, y_pl, mx, mapy)) return false;
            if (!sn.add_draw(plan, static_cast<u16>(x + sn.tile_width()), y_pl, static_cast<u16>(mx + 1), mapy)) return false;
        }

        // POST-incremento. videoposy envuelve en el bucle vertical del display.
        ++m_state.mapposy;
        m_state.videoposy = static_cast<s32>(m_state.mapposy % sn.display_height());

        if (stepy == static_cast<u16>(sn.tile_height() - 1) && stepx) {
            // Fila completada: ajustar la columna de fillup (mapblocky POST).
            const u16 nvpos = block_videoposy(sn);
            const u16 nmapblocky = static_cast<u16>(mapblocky + 1);
            if (!sn.add_draw(plan, x0, static_cast<u16>(nvpos * sn.planes()), mapblockx, nmapblocky)) return false;
            if (!sn.one_direction() && m_state.previous_xdirection == ScrollDirLeft) sn.restore_saveword();
            const u16 my = static_cast<u16>(stepx + 1);
            const u16 y2 = static_cast<u16>(((nvpos + my * sn.tile_height()) % sn.display_height()) * sn.planes());
            const u16 y2b = static_cast<u16>((y2 + sn.block_planes_lines() - 1) % sn.display_planelines());
            sn.save_word(static_cast<u32>(y2b) * sn.bytes_per_row() + ((x0 + sn.bitmap_width()) / 8u));
            if (!sn.add_draw(plan, static_cast<u16>(x0 + sn.bitmap_width()), y2,
                    static_cast<u16>(mapblockx + sn.bitmap_blocks_per_row()),
                    static_cast<u16>(my + nmapblocky))) return false;
            m_state.previous_xdirection = ScrollDirRight;
        }
        return true;
    }

    /// Scroll vertical 1 px hacia arriba — ScrollUp corkscrew.
    /// Fiel a ScrollUp de Scroller_XYLimited/main.c:529-637.
    bool scroll_up(graphics::FramePlan& plan, Sink& sn) {
        if (m_state.mapposy < 1) return false;
        --m_state.mapposy;
        m_state.videoposy = static_cast<s32>(m_state.mapposy % sn.display_height());

        const u16 mapblockx = static_cast<u16>(m_state.mapposx / sn.tile_width());
        const u16 mapblocky = static_cast<u16>(m_state.mapposy / sn.tile_height());
        const u16 stepx = static_cast<u16>(m_state.mapposx & (sn.tile_width() - 1));
        const u16 stepy = static_cast<u16>(m_state.mapposy & (sn.tile_height() - 1));
        const u16 bvpos = block_videoposy(sn);
        const u16 x0 = static_cast<u16>(m_state.videoposx & ~(sn.tile_width() - 1));
        const u16 y_pl = static_cast<u16>(bvpos * sn.planes());

        if (stepy == static_cast<u16>(sn.tile_height() - 1) && stepx) {
            // Columna completada: ajustar la fila de fillup.
            const u16 mx1 = static_cast<u16>(mapblockx + sn.bitmap_blocks_per_row());
            const u16 y1 = static_cast<u16>(((bvpos + sn.tile_height()) % sn.display_height()) * sn.planes());
            if (!sn.add_draw(plan, static_cast<u16>(x0 + sn.bitmap_width()), y1, mx1, static_cast<u16>(mapblocky + 1))) return false;
            if (m_state.previous_xdirection == ScrollDirRight) sn.restore_saveword();
            const u16 my2 = static_cast<u16>(stepx + 2);
            const u16 y2 = static_cast<u16>(((bvpos + my2 * sn.tile_height()) % sn.display_height()) * sn.planes());
            sn.save_word(static_cast<u32>(y2) * sn.bytes_per_row() + (x0 / 8u));
            if (!sn.add_draw(plan, x0, y2,
                    static_cast<u16>(mx1 - sn.bitmap_blocks_per_row()),
                    static_cast<u16>(my2 + mapblocky))) return false;
            m_state.previous_xdirection = ScrollDirLeft;
        }

        // Fila entrante en la banda de staging (map row = mapblocky).
        if (stepy >= twoblockstep(sn)) {
            const u16 mx = static_cast<u16>(stepy + twoblockstep(sn) + mapblockx);
            const u16 x = static_cast<u16>((stepy + twoblockstep(sn)) * sn.tile_width() + x0);
            if (!sn.add_draw(plan, x, y_pl, mx, mapblocky)) return false;
        } else {
            const u16 mx = static_cast<u16>(stepy * 2 + mapblockx);
            const u16 x = static_cast<u16>(stepy * 2 * sn.tile_width() + x0);
            if (!sn.add_draw(plan, x, y_pl, mx, mapblocky)) return false;
            if (!sn.add_draw(plan, static_cast<u16>(x + sn.tile_width()), y_pl, static_cast<u16>(mx + 1), mapblocky)) return false;
        }
        return true;
    }

private:
    ScrollState m_state {};
};

} // namespace eng::field