#pragma once

/// \file scroll_engine.hpp
/// Driver + algoritmo del scroll separados del playfield (diseño §7 `ScrollEngine`).
///
/// RESPONSABILIDAD: es el ALGORITMO (cámara + movimientos 1 px del
/// corkscrew/XYLimited de Steger), independiente de la superficie concreta:
/// opera sobre un *sink* genérico (concepto `ScrollSink`, ver abajo). No posee
/// memoria ni geometría: el sink (p. ej. `XLimitedPlayfield`) aporta el layout.
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
/// GEOMETRÍA COMPILE-TIME (fast_div.hpp): los denominadores calientes
/// (tile width/height, display_height, display_planelines, planes) se pasan
/// como NTTP `ScrollConsts`. Si un campo es distinto de 0 el engine usa
/// `eng::fast_div<>` (potencia de dos → shifts; constante general → multiplicación
/// mágica) y NUNCA llama a `__udivsi3`. Si es 0, se cae a la geometría runtime del
/// sink (división nativa completa): es el fallback del peor caso, correcto pero
/// lento. Los valores a priori (K_TILE_W=16 etc.) deben entrar como constantes.
///
/// Reglas del engine: sin heap, sin RTTI, sin virtuals en el hot path (sink por
/// template), gnu++23.

#include <eng/core/fast_div.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/scope_guard.hpp>
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

/// Estrategia de scroll trivial para una escena YA dibujada ("big buffer"): no hay
/// anillo ni banda de staging que rellenar; el único estado es el offset de cámara
/// y el display resuelve moviendo el puntero/BPLxPT sobre el buffer completo. Es la
/// estrategia base para contenido que cabe entero (o se pinta por otro medio) y
/// sirve de contraste con `ScrollEngine` (corkscrew/XYLimited, que sí emite blits).
///
/// No conoce hardware: solo lleva la cámara acotada al rango del buffer.
struct BigBufferScroll {
    s32 position = 0;   // offset de cámara en X (px)
    s32 min_pos = 0;    // límite inferior
    s32 max_pos = 0;    // límite superior (mundo - viewport)
    bool clamp = true;  // false = anillo (envuelve sin recortar)

    /// Avanza el offset `dx` px (clamp opcional al rango) y devuelve la posición.
    s32 step(s32 dx) {
        position += dx;
        if (clamp) {
            if (position < min_pos) position = min_pos;
            if (position > max_pos) position = max_pos;
        }
        return position;
    }
    constexpr void reset(s32 p = 0) { position = p; }
};

/// Constantes de geometría para el hot path (0 = runtime desde el sink). Cuando
/// se conocen a priori (potencias de dos casi siempre), el engine usa `fast_div`
/// y evita las divisiones por frame.
struct ScrollConsts {
    u32 tile_width = 0;        // 0 = sn.tile_width()
    u32 tile_height = 0;       // 0 = sn.tile_height()
    u32 display_height = 0;    // 0 = sn.display_height()
    u32 display_planelines = 0;// 0 = sn.display_planelines()
    u32 planes = 0;            // 0 = sn.planes()
};

/// Geometría del anillo/layout que una estrategia de scroll consulta (SIN dibujar):
/// tile/planes, viewport, bucle del display, bloques por fila/columna, bytes por
/// fila y límites/wrap del mapa. Es la mitad "layout" del sink.
template <class S>
concept ScrollTarget = requires(const S& s) {
    s.tile_width();
    s.tile_height();
    s.planes();
    s.viewport_w();
    s.viewport_h();
    s.display_height();
    s.display_planelines();
    s.bitmap_blocks_per_row();
    s.bitmap_blocks_per_col();
    s.block_planes_lines();
    s.bytes_per_row();
    s.bitmap_width();
    s.map_width_blocks();
    s.map_height_blocks();
    s.map_wrap_x();
    s.map_wrap_y();
    s.one_direction();
};

/// Emisión de dibujo + costura del layout: pintar la banda entrante (`add_draw`)
/// y guardar/restaurar la word de la costura. Es la mitad "cómo se dibuja" del sink.
template <class S>
concept ScrollEmitter = requires(graphics::FramePlan& plan, S& s, u16 x, u32 o) {
    s.add_draw(plan, x, x, x, x);  // bool (el uso lo valida con `!sn.add_draw(...)`)
    s.save_word(o);
    s.restore_saveword();
};

/// Contrato completo del algoritmo: geometría (`ScrollTarget`) + dibujo (`ScrollEmitter`).
/// Cualquier superficie que cumpla ambas mitades sirve de sink al `ScrollEngine`
/// (en Amiga, `XLimitedPlayfield`). Se comprueba en compile-time con un `static_assert`
/// en el playfield. El `ScrollConsts` NTTP permite además saltarse el sink cuando la
/// geometría se conoce a priori.
template <class S>
concept ScrollSink = ScrollTarget<S> && ScrollEmitter<S>;

/// Driver del scroll: estado + algoritmo de los 4 movimientos.
template <class Sink, ScrollConsts C = ScrollConsts{}>
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

    // --- Geometría: constante NTTP si se conoce, runtime del sink si no. -----
    inline u16 tw(const Sink& sn) const {
        return C.tile_width ? static_cast<u16>(C.tile_width) : sn.tile_width();
    }
    inline u16 th(const Sink& sn) const {
        return C.tile_height ? static_cast<u16>(C.tile_height) : sn.tile_height();
    }
    inline u32 dh(const Sink& sn) const {
        return C.display_height ? C.display_height : sn.display_height();
    }
    inline u32 dph(const Sink& sn) const {
        return C.display_planelines ? C.display_planelines : sn.display_planelines();
    }
    inline u8 planes(const Sink& sn) const {
        return C.planes ? static_cast<u8>(C.planes) : sn.planes();
    }
    /// ¿El eje X es lineal acotado (sin anillo)? Opcional en el sink (false por
    /// defecto = XLimited de anillo, comportamiento histórico).
    inline bool finite_x(const Sink& sn) const {
        if constexpr (requires { sn.finite_x(); }) return sn.finite_x();
        else return false;
    }
    // Cociente/resto por tile_width: shift/mask si C.tile_width es potencia de 2.
    inline u32 q_tw(const Sink& sn, s32 v) const {
        if constexpr (C.tile_width != 0u) return fast_div<C.tile_width>::q(static_cast<u32>(v));
        return static_cast<u32>(v) / sn.tile_width();
    }
    inline u32 r_tw(const Sink& sn, s32 v) const {
        if constexpr (C.tile_width != 0u) return fast_div<C.tile_width>::r(static_cast<u32>(v));
        return static_cast<u32>(v) % sn.tile_width();
    }
    inline u32 q_th(const Sink& sn, s32 v) const {
        if constexpr (C.tile_height != 0u) return fast_div<C.tile_height>::q(static_cast<u32>(v));
        return static_cast<u32>(v) / sn.tile_height();
    }
    inline u32 r_th(const Sink& sn, s32 v) const {
        if constexpr (C.tile_height != 0u) return fast_div<C.tile_height>::r(static_cast<u32>(v));
        return static_cast<u32>(v) % sn.tile_height();
    }
    // Resto por display_height / display_planelines (cociente no hace falta).
    inline u32 r_dh(const Sink& sn, u32 v) const {
        if constexpr (C.display_height != 0u) return fast_div<C.display_height>::r(v);
        return v % sn.display_height();
    }
    inline u32 r_dph(const Sink& sn, u32 v) const {
        if constexpr (C.display_planelines != 0u) return fast_div<C.display_planelines>::r(v);
        return v % sn.display_planelines();
    }

    /// Fila de bloque del mapa que el display está mostrando (0..display_height).
    inline u32 block_videoposy(const Sink& sn) const {
        // La banda de staging debe envolver SIEMPRE en el bucle vertical del
        // display (display_height = viewport_h + 2*tile_height), nunca en el
        // bitmap físico (bitmap_height incluye las filas extra del walk X).
        return r_dh(sn, q_th(sn, m_state.mapposy) * th(sn));
    }
    /// Constante de "dos bloques" del corkscrew (fillup a dos bloques de ancho).
    inline u16 twoblockstep(const Sink& sn) const {
        return sn.bitmap_blocks_per_row() > th(sn)
            ? static_cast<u16>(sn.bitmap_blocks_per_row() - th(sn)) : 0;
    }

    /// Scroll de 1 px a la derecha (plane-shifted) — ScrollRight corkscrew.
    /// Fiel a ScrollRight de Scroller_XYLimited/main.c:869-978:
    ///   columna entrante x = BITMAPWIDTH + ROUND2BLOCKWIDTH(videoposx),
    ///   fila mapy = stepx+1 (2 bloques si stepx==0), y = (block_videoposy +
    ///   mapy*TH) % display_height, ajuste de la fila de fillup al completar.
    bool scroll_right(graphics::FramePlan& plan, Sink& sn) {
        if (finite_x(sn)) {
            // Eje X lineal acotado: solo mueve el puntero (el contenido ya está;
            // sin creep, sin banda entrante, sin guardas laterales).
            const s32 limit = static_cast<s32>(sn.map_width_blocks()) * tw(sn) - sn.viewport_w();
            if (sn.map_wrap_x() == 0 && m_state.mapposx >= limit) return false;
            ++m_state.mapposx;
            m_state.videoposx = m_state.mapposx;
            return true;
        }
        const s32 limit = static_cast<s32>(sn.map_width_blocks()) * tw(sn) -
                          sn.viewport_w() - tw(sn);
        if (sn.map_wrap_x() == 0 && m_state.mapposx >= limit) return false;

        const u16 mapblockx = static_cast<u16>(q_tw(sn, m_state.mapposx));
        const u16 mapblocky = static_cast<u16>(q_th(sn, m_state.mapposy));
        const u16 stepx = static_cast<u16>(r_tw(sn, m_state.mapposx));
        const u16 stepy = static_cast<u16>(r_th(sn, m_state.mapposy));
        const u32 bvpos = block_videoposy(sn);
        const u16 x0 = static_cast<u16>(m_state.videoposx & ~(tw(sn) - 1));
        const u16 mapx = static_cast<u16>(mapblockx + sn.bitmap_blocks_per_row());

        if (!sn.one_direction() && m_state.previous_xdirection == ScrollDirLeft) sn.restore_saveword();

        // La costura se guarda con `save_word` y hay que restaurarla si el frame se
        // rechaza despues (add_draw puede devolver false): sin esto, un fallo dejaba
        // la word de costura a medio escribir. El guard la restaura en cualquier salida
        // de error; `release()` la desactiva en el camino correcto.
        bool saveword_armed = false;
        auto restore_on_error = eng::util::make_scope_guard([&] {
            if (saveword_armed) sn.restore_saveword();
        });

        u16 mapy = static_cast<u16>(stepx + 1);
        if (mapy == 1) { // stepx == 0 → dos bloques
            mapy = static_cast<u16>(mapy + mapblocky);
            const u32 y = r_dh(sn, bvpos + th(sn)) * planes(sn);
            if (!sn.add_draw(plan, static_cast<u16>(x0 + sn.bitmap_width()), static_cast<u16>(y), mapx, mapy)) return false;
            const u32 y2 = r_dph(sn, y + sn.block_planes_lines());
            sn.save_word((y2 + sn.block_planes_lines() - 1u) * sn.bytes_per_row() +
                         ((x0 + sn.bitmap_width()) / 8u));
            saveword_armed = true;
            if (!sn.add_draw(plan, static_cast<u16>(x0 + sn.bitmap_width()), static_cast<u16>(y2), mapx, static_cast<u16>(mapy + 1))) return false;
        } else { // un bloque
            ++mapy;
            const u32 y = r_dh(sn, bvpos + mapy * th(sn)) * planes(sn);
            mapy = static_cast<u16>(mapy + mapblocky);
            sn.save_word((y + sn.block_planes_lines() - 1u) * sn.bytes_per_row() +
                         ((x0 + sn.bitmap_width()) / 8u));
            saveword_armed = true;
            if (!sn.add_draw(plan, static_cast<u16>(x0 + sn.bitmap_width()), static_cast<u16>(y), mapx, mapy)) return false;
        }

        ++m_state.mapposx;
        m_state.videoposx = m_state.mapposx;
        const u16 new_stepx = static_cast<u16>(r_tw(sn, m_state.mapposx));

        if (new_stepx == 0) {
            // Columna completada: ajustar la fila de fillup (valores POST-incremento).
            const u16 nx0 = static_cast<u16>(x0 + tw(sn));
            const u16 nmapblockx = static_cast<u16>(mapblockx + 1);
            if (!sn.add_draw(plan,
                    static_cast<u16>(nx0 + (sn.bitmap_blocks_per_row() - 1) * tw(sn)),
                    static_cast<u16>(bvpos * planes(sn)),
                    static_cast<u16>(nmapblockx + sn.bitmap_blocks_per_row() - 1), mapblocky)) return false;
            if (stepy) {
                const u16 mx = stepy >= twoblockstep(sn)
                    ? static_cast<u16>(stepy + (twoblockstep(sn) - 1))
                    : static_cast<u16>(stepy * 2 - 1);
                if (!sn.add_draw(plan,
                        static_cast<u16>(nx0 + mx * tw(sn)),
                        static_cast<u16>(bvpos * planes(sn)),
                        static_cast<u16>(mx + nmapblockx),
                        static_cast<u16>(mapblocky + sn.bitmap_blocks_per_col()))) return false;
            }
        }

        m_state.previous_xdirection = new_stepx ? ScrollDirRight : ScrollDirNone;
        saveword_armed = false; // camino correcto: la costura queda como este frame manda
        return true;
    }

    /// Avanza hasta `tiles` TILES a la derecha (anillo XLimited) en una sola
    /// pasada: calcula la geometría del cruce UNA vez en lugar de por cada píxel
    /// (menos procesamiento por px en los perfiles con `prefill`). Emite
    /// exactamente los mismos blits y ajustes que `tiles*tile_width` pasos de
    /// `scroll_right` (verificado en HOST-034 con un sink de registro). Requiere
    /// `mapposx` alineado a tile al entrar (lo garantiza `snap_to_tiles`).
    ///
    /// La FUSIÓN de tiles (un blit por varias filas de bloque) viviría aquí en el
    /// futuro; hoy el nº de blits es el mismo y el ahorro es de cálculo por px.
    bool burst_right(graphics::FramePlan& plan, Sink& sn, u8 tiles) {
        if (finite_x(sn)) {
            const u16 n = static_cast<u16>(tiles) * tw(sn);
            for (u16 i = 0; i < n; ++i) if (!scroll_right(plan, sn)) return false;
            return true;
        }
        const u16 twv = tw(sn);
        const u16 thv = th(sn);
        const u8 pl = planes(sn);
        const u16 bpr = sn.bitmap_blocks_per_row();
        const u16 bw = sn.bitmap_width();
        const u16 bpll = sn.block_planes_lines();
        const u16 bpr_bytes = sn.bytes_per_row();
        const u16 tbs = twoblockstep(sn);
        for (u8 t = 0; t < tiles; ++t) {
            if (sn.map_wrap_x() == 0) {
                const s32 limit = static_cast<s32>(sn.map_width_blocks()) * twv - sn.viewport_w() - twv;
                if (m_state.mapposx > limit) return false;
            }
            const u16 mapblockx = static_cast<u16>(q_tw(sn, m_state.mapposx));
            const u16 mapblocky = static_cast<u16>(q_th(sn, m_state.mapposy));
            const u16 stepy = static_cast<u16>(r_th(sn, m_state.mapposy));
            const u32 bvpos = block_videoposy(sn);
            const u16 x0 = static_cast<u16>(m_state.videoposx & ~(twv - 1));
            const u16 mapx = static_cast<u16>(mapblockx + bpr);

            if (!sn.one_direction() && m_state.previous_xdirection == ScrollDirLeft) sn.restore_saveword();

            // Igual que `scroll_right`: la costura debe restaurarse si el frame se
            // rechaza despues de `save_word`.
            bool saveword_armed = false;
            auto restore_on_error = eng::util::make_scope_guard([&] {
                if (saveword_armed) sn.restore_saveword();
            });

            // Pixel k=0 (stepx=0): dos bloques de la columna entrante.
            {
                const u32 y = r_dh(sn, bvpos + thv) * pl;
                if (!sn.add_draw(plan, static_cast<u16>(x0 + bw), static_cast<u16>(y), mapx,
                        static_cast<u16>(mapblocky + 1))) return false;
                const u32 y2 = r_dph(sn, y + bpll);
                sn.save_word((y2 + bpll - 1u) * bpr_bytes + ((x0 + bw) / 8u));
                saveword_armed = true;
                if (!sn.add_draw(plan, static_cast<u16>(x0 + bw), static_cast<u16>(y2), mapx,
                        static_cast<u16>(mapblocky + 2))) return false;
            }
            // Pixels k=1..tw-1: un bloque cada uno (mapy = k+2).
            for (u16 k = 1; k < twv; ++k) {
                const u16 mapy = static_cast<u16>(k + 2u);
                const u32 y = r_dh(sn, bvpos + mapy * thv) * pl;
                sn.save_word((y + bpll - 1u) * bpr_bytes + ((x0 + bw) / 8u));
                saveword_armed = true;
                if (!sn.add_draw(plan, static_cast<u16>(x0 + bw), static_cast<u16>(y), mapx,
                        static_cast<u16>(mapy + mapblocky))) return false;
            }
            // Avance + ajuste de la fila de fillup (post stepx == 0).
            m_state.mapposx += twv;
            m_state.videoposx = m_state.mapposx;
            const u16 nx0 = static_cast<u16>(x0 + twv);
            const u16 nmapblockx = static_cast<u16>(mapblockx + 1u);
            if (!sn.add_draw(plan, static_cast<u16>(nx0 + (bpr - 1u) * twv), static_cast<u16>(bvpos * pl),
                    static_cast<u16>(nmapblockx + bpr - 1u), mapblocky)) return false;
            if (stepy) {
                const u16 mx = stepy >= tbs ? static_cast<u16>(stepy + (tbs - 1u))
                                            : static_cast<u16>(stepy * 2u - 1u);
                if (!sn.add_draw(plan, static_cast<u16>(nx0 + mx * twv), static_cast<u16>(bvpos * pl),
                        static_cast<u16>(mx + nmapblockx),
                        static_cast<u16>(mapblocky + sn.bitmap_blocks_per_col()))) return false;
            }
            m_state.previous_xdirection = ScrollDirNone;
            saveword_armed = false; // camino correcto
        }
        return true;
    }

    /// Scroll de 1 px a la izquierda (no plane-shifted) — ScrollLeft corkscrew.
    /// Fiel a ScrollLeft de Scroller_XYLimited/main.c:751-867.
    bool scroll_left(graphics::FramePlan& plan, Sink& sn) {
        if (finite_x(sn)) {
            if (m_state.mapposx < 1) return false;
            --m_state.mapposx;
            m_state.videoposx = m_state.mapposx;
            return true;
        }
        if (m_state.mapposx < 1) return false;
        --m_state.mapposx;
        m_state.videoposx = m_state.mapposx;

        const u16 mapblockx = static_cast<u16>(q_tw(sn, m_state.mapposx));
        const u16 mapblocky = static_cast<u16>(q_th(sn, m_state.mapposy));
        const u16 stepx = static_cast<u16>(r_tw(sn, m_state.mapposx));
        const u16 stepy = static_cast<u16>(r_th(sn, m_state.mapposy));
        const u32 bvpos = block_videoposy(sn);
        const u16 x0 = static_cast<u16>(m_state.videoposx & ~(tw(sn) - 1));

        if (stepx == static_cast<u16>(tw(sn) - 1)) {
            // Columna completada: ajustar la fila de fillup.
            u16 mapx = mapblockx;
            u16 mapy = mapblocky;
            if (stepy) mapy = static_cast<u16>(mapy + sn.bitmap_blocks_per_col());
            if (!sn.add_draw(plan, x0, static_cast<u16>(bvpos * planes(sn)), mapx, mapy)) return false;
            mapx = stepy;
            if (mapx) {
                mapx = mapx >= twoblockstep(sn)
                    ? static_cast<u16>(mapx + twoblockstep(sn))
                    : static_cast<u16>(mapx * 2);
                if (!sn.add_draw(plan,
                        static_cast<u16>(x0 + mapx * tw(sn)),
                        static_cast<u16>(bvpos * planes(sn)),
                        static_cast<u16>(mapx + mapblockx),
                        static_cast<u16>(mapy - sn.bitmap_blocks_per_col()))) return false;
            }
        }

        const u16 mapx = mapblockx;
        u16 mapy = static_cast<u16>(stepx + 1);
        if (!sn.one_direction() && m_state.previous_xdirection == ScrollDirRight) sn.restore_saveword();
        bool saveword_armed = false;
        auto restore_on_error = eng::util::make_scope_guard([&] {
            if (saveword_armed) sn.restore_saveword();
        });
        if (mapy == 1) { // stepx == 0 → dos bloques
            mapy = static_cast<u16>(mapy + mapblocky);
            const u32 y = r_dh(sn, bvpos + th(sn)) * planes(sn);
            sn.save_word(y * sn.bytes_per_row() + (x0 / 8u));
            saveword_armed = true;
            if (!sn.add_draw(plan, x0, static_cast<u16>(y), mapx, mapy)) return false;
            const u32 y2 = r_dph(sn, y + sn.block_planes_lines());
            if (!sn.add_draw(plan, x0, static_cast<u16>(y2), mapx, static_cast<u16>(mapy + 1))) return false;
        } else { // un bloque
            ++mapy;
            const u32 y = r_dh(sn, bvpos + mapy * th(sn)) * planes(sn);
            mapy = static_cast<u16>(mapy + mapblocky);
            sn.save_word(y * sn.bytes_per_row() + (x0 / 8u));
            saveword_armed = true;
            if (!sn.add_draw(plan, x0, static_cast<u16>(y), mapx, mapy)) return false;
        }

        m_state.previous_xdirection = stepx ? ScrollDirLeft : ScrollDirNone;
        saveword_armed = false; // camino correcto
        return true;
    }

    /// Scroll vertical 1 px hacia abajo — ScrollDown corkscrew.
    /// Fiel a ScrollDown de Scroller_XYLimited/main.c:639-749.
    bool scroll_down(graphics::FramePlan& plan, Sink& sn) {
        if (finite_x(sn)) {
            // Y corkscrew con X lineal: la fila entrante ocupa TODO el ancho del
            // bitmap (no hay desplazamiento plane-shifted del anillo X). Se pinta
            // UNA vez por fila de mapa (no por pixel): la banda se revela luego.
            const s32 limitY = static_cast<s32>(sn.map_height_blocks()) * th(sn) - sn.viewport_h();
            if (sn.map_wrap_y() == 0 && m_state.mapposy >= limitY) return false;
            if (r_th(sn, m_state.mapposy) == 0u) {
                const u32 y_pl = block_videoposy(sn) * planes(sn);
                const u16 mapy = static_cast<u16>(q_th(sn, m_state.mapposy) + sn.bitmap_blocks_per_col());
                const u16 cols = sn.bitmap_blocks_per_row();
                for (u16 c = 0; c < cols; ++c) {
                    if (!sn.add_draw(plan, static_cast<u16>(c * tw(sn)), static_cast<u16>(y_pl), c, mapy)) return false;
                }
            }
            ++m_state.mapposy;
            m_state.videoposy = static_cast<s32>(r_dh(sn, static_cast<u32>(m_state.mapposy)));
            return true;
        }
        const s32 limitY = static_cast<s32>(sn.map_height_blocks()) * th(sn) -
                           sn.viewport_h() - th(sn);
        if (sn.map_wrap_y() == 0 && m_state.mapposy >= limitY) return false;

        const u16 mapblockx = static_cast<u16>(q_tw(sn, m_state.mapposx));
        const u16 mapblocky = static_cast<u16>(q_th(sn, m_state.mapposy));
        const u16 stepx = static_cast<u16>(r_tw(sn, m_state.mapposx));
        const u16 stepy = static_cast<u16>(r_th(sn, m_state.mapposy));
        const u32 bvpos = block_videoposy(sn);
        const u16 x0 = static_cast<u16>(m_state.videoposx & ~(tw(sn) - 1));
        const u32 y_pl = bvpos * planes(sn);
        const u16 mapy = static_cast<u16>(mapblocky + sn.bitmap_blocks_per_col());

        // Fila entrante en la banda de staging (valores PRE-incremento).
        if (stepy >= twoblockstep(sn)) {
            const u16 mx = static_cast<u16>(stepy + twoblockstep(sn) + mapblockx);
            const u16 x = static_cast<u16>((stepy + twoblockstep(sn)) * tw(sn) + x0);
            if (!sn.add_draw(plan, x, static_cast<u16>(y_pl), mx, mapy)) return false;
        } else {
            const u16 mx = static_cast<u16>(stepy * 2 + mapblockx);
            const u16 x = static_cast<u16>(stepy * 2 * tw(sn) + x0);
            if (!sn.add_draw(plan, x, static_cast<u16>(y_pl), mx, mapy)) return false;
            if (!sn.add_draw(plan, static_cast<u16>(x + tw(sn)), static_cast<u16>(y_pl), static_cast<u16>(mx + 1), mapy)) return false;
        }

        // POST-incremento. videoposy envuelve en el bucle vertical del display.
        ++m_state.mapposy;
        m_state.videoposy = static_cast<s32>(r_dh(sn, static_cast<u32>(m_state.mapposy)));

        if (stepy == static_cast<u16>(th(sn) - 1) && stepx) {
            // Fila completada: ajustar la columna de fillup (mapblocky POST).
            const u32 nvpos = block_videoposy(sn);
            const u16 nmapblocky = static_cast<u16>(mapblocky + 1);
            if (!sn.add_draw(plan, x0, static_cast<u16>(nvpos * planes(sn)), mapblockx, nmapblocky)) return false;
            if (!sn.one_direction() && m_state.previous_xdirection == ScrollDirLeft) sn.restore_saveword();
            const u16 my = static_cast<u16>(stepx + 1);
            const u32 y2 = r_dh(sn, nvpos + my * th(sn)) * planes(sn);
            const u32 y2b = r_dph(sn, y2 + sn.block_planes_lines() - 1u);
            sn.save_word(y2b * sn.bytes_per_row() + ((x0 + sn.bitmap_width()) / 8u));
            auto restore_on_error = eng::util::make_scope_guard([&] { sn.restore_saveword(); });
            if (!sn.add_draw(plan, static_cast<u16>(x0 + sn.bitmap_width()), static_cast<u16>(y2),
                    static_cast<u16>(mapblockx + sn.bitmap_blocks_per_row()),
                    static_cast<u16>(my + nmapblocky))) return false;
            restore_on_error.release(); // camino correcto
            m_state.previous_xdirection = ScrollDirRight;
        }
        return true;
    }

    /// Scroll vertical 1 px hacia arriba — ScrollUp corkscrew.
    /// Fiel a ScrollUp de Scroller_XYLimited/main.c:529-637.
    ///
    /// NOTA sobre el toroide vertical INVERSO: NO se salta `mapposy` al borde
    /// inferior cuando mapposy<1 (un salto 0->map_height deja el anillo con
    /// contenido obsoleto -> ~una pantalla de basura hasta que el fillup lo
    /// redibuja). El corkscrew mantiene el anillo como ventana deslizante SOLO
    /// para avances continuos de 1 px; cruzar el borde del toroide requiere un
    /// re-fill del anillo (ver README §7.9). Por eso aquí se BLOQUEA en
    /// mapposy<1 (igual que ScrollLeft): la demo 201 acota la cámara a [0,max].
    bool scroll_up(graphics::FramePlan& plan, Sink& sn) {
        if (finite_x(sn)) {
            if (m_state.mapposy < 1) return false;
            const u16 row_before = static_cast<u16>(q_th(sn, m_state.mapposy));
            --m_state.mapposy;
            m_state.videoposy = static_cast<s32>(r_dh(sn, static_cast<u32>(m_state.mapposy)));
            // Pintar la fila entrante UNA vez al cruzar de fila de mapa.
            if (static_cast<u16>(q_th(sn, m_state.mapposy)) != row_before) {
                const u32 y_pl = block_videoposy(sn) * planes(sn);
                const u16 mapy = static_cast<u16>(q_th(sn, m_state.mapposy));
                const u16 cols = sn.bitmap_blocks_per_row();
                for (u16 c = 0; c < cols; ++c) {
                    if (!sn.add_draw(plan, static_cast<u16>(c * tw(sn)), static_cast<u16>(y_pl), c, mapy)) return false;
                }
            }
            return true;
        }
        if (m_state.mapposy < 1) return false;
        --m_state.mapposy;
        m_state.videoposy = static_cast<s32>(r_dh(sn, static_cast<u32>(m_state.mapposy)));

        const u16 mapblockx = static_cast<u16>(q_tw(sn, m_state.mapposx));
        const u16 mapblocky = static_cast<u16>(q_th(sn, m_state.mapposy));
        const u16 stepx = static_cast<u16>(r_tw(sn, m_state.mapposx));
        const u16 stepy = static_cast<u16>(r_th(sn, m_state.mapposy));
        const u32 bvpos = block_videoposy(sn);
        const u16 x0 = static_cast<u16>(m_state.videoposx & ~(tw(sn) - 1));
        const u32 y_pl = bvpos * planes(sn);

        if (stepy == static_cast<u16>(th(sn) - 1) && stepx) {
            // Columna completada: ajustar la fila de fillup.
            const u16 mx1 = static_cast<u16>(mapblockx + sn.bitmap_blocks_per_row());
            const u32 y1 = r_dh(sn, bvpos + th(sn)) * planes(sn);
            if (!sn.add_draw(plan, static_cast<u16>(x0 + sn.bitmap_width()), static_cast<u16>(y1), mx1, static_cast<u16>(mapblocky + 1))) return false;
            if (!sn.one_direction() && m_state.previous_xdirection == ScrollDirRight) sn.restore_saveword();
            const u16 my2 = static_cast<u16>(stepx + 2);
            const u32 y2 = r_dh(sn, bvpos + my2 * th(sn)) * planes(sn);
            sn.save_word(y2 * sn.bytes_per_row() + (x0 / 8u));
            auto restore_on_error = eng::util::make_scope_guard([&] { sn.restore_saveword(); });
            if (!sn.add_draw(plan, x0, static_cast<u16>(y2),
                    static_cast<u16>(mx1 - sn.bitmap_blocks_per_row()),
                    static_cast<u16>(my2 + mapblocky))) return false;
            restore_on_error.release(); // camino correcto
            m_state.previous_xdirection = ScrollDirLeft;
        }

        // Fila entrante en la banda de staging (map row = mapblocky).
        if (stepy >= twoblockstep(sn)) {
            const u16 mx = static_cast<u16>(stepy + twoblockstep(sn) + mapblockx);
            const u16 x = static_cast<u16>((stepy + twoblockstep(sn)) * tw(sn) + x0);
            if (!sn.add_draw(plan, x, static_cast<u16>(y_pl), mx, mapblocky)) return false;
        } else {
            const u16 mx = static_cast<u16>(stepy * 2 + mapblockx);
            const u16 x = static_cast<u16>(stepy * 2 * tw(sn) + x0);
            if (!sn.add_draw(plan, x, static_cast<u16>(y_pl), mx, mapblocky)) return false;
            if (!sn.add_draw(plan, static_cast<u16>(x + tw(sn)), static_cast<u16>(y_pl), static_cast<u16>(mx + 1), mapblocky)) return false;
        }
        return true;
    }

private:
    ScrollState m_state {};
};

} // namespace eng::field
