#pragma once

/// \file playfield.hpp
/// Abstracción de capa gráfica (playfield) reutilizable sobre el chipset Amiga.
///
/// Un `Playfield` es un objeto de primera clase que posee su framebuffer (Chip
/// RAM), su geometría y sus primitivas de dibujo. El scroll es una
/// ESPECIALIZACIÓN del playfield: cada tipo concreto decide el mapeo lógico→
/// físico (planelínea, byte, costura del split, espejo del modo lineal) y cómo
/// scrollea (o si no scrollea). La `Scene` es un objeto contenedor que compone
/// varios playfields + sprites + paletas + copperlist; nunca se accede a un
/// playfield por índice.
///
/// Clases:
///   - `Playfield`            base abstracta: framebuffer + geometría + primitivas
///                            CPU implementadas en la base vía hooks de mapeo.
///   - `CanvasPlayfield`      lienzo plano sin tiles ni scroll (para blits y
///                            primitivas de CPU; p. ej. un HUD).
///   - `XLimitedPlayfield`    (xlimited.hpp) corkscrew 8-way con variantes.
///
/// Reglas del engine: sin heap, sin RTTI, gnu++23. Toda escritura al framebuffer
/// pasa por las primitivas (`Surface` y blits con `Span`), que devuelven `bool`
/// y validan. La API pública NO expone el puntero crudo del framebuffer: solo
/// `Surface` (que enruta por el mapeo) y blits con `Span` (el tamaño es el
/// contrato). El acceso crudo optimizado vive dentro del engine (núcleo), no en
/// el código de la aplicación.

#include <eng/core/arith.hpp>
#include <eng/core/polygon.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/graphics/bitmap.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

/// **Operación lógica** de una escritura sobre el bitmap: qué hace el dato con el
/// contenido previo. El CPU la aplica con lógica de palabras; el Blitter, con el
/// `minterm` equivalente (`BlitJob::minterm`). Permite una API de dibujo uniforme
/// (`Surface::fill_rect(..., RasterOp::Xor)`) independiente de la implementación.
enum class RasterOp : eng::u8 {
	Copy = 0,  ///< `D = color` (por máscara)
	Or = 1,    ///< `D |= color`
	And = 2,   ///< `D &= color`
	Xor = 3,   ///< `D ^= color`
	Clear = 4, ///< `D = 0` (borrado)
};

/// **Modo de aceleración** de dibujo: elige la implementación (CPU/Blitter) de las
/// operaciones de `Surface`. `Auto` decide por coste (área frente a `setup_cycles`).
enum class AccelMode : eng::u8 {
	Auto = 0,   ///< el rasterizador decide por coste
	Cpu = 1,    ///< fuerza CPU
	Blitter = 2 ///< prefiere Blitter (si el backend lo declara)
};

/// **Capacidades de rasterizado** que declara el backend: si hay Blitter y qué sabe
/// hacer. El `RasterPolicy` (de la app) se contrasta con esto para elegir CPU/Blitter.
struct RasterCaps {
	bool blitter = false;        ///< ¿hay Blitter?
	eng::u8 bus = 16;            ///< ancho de bus (16 OCS; 32/64 AGA con FMODE)
	bool fill = false;           ///< relleno de polígonos (BLTCON fill)
	bool line = false;           ///< trazado de líneas (BLTCON line)
	bool shift = false;          ///< shifts A/B (origen no alineado a 16)
	bool minterms = false;       ///< operaciones lógicas (minterm)
	eng::u16 setup_cycles = 60;  ///< coste de arrancar un blit (para `Auto`)
};

/// **Política de rasterizado** (la elige la app/escena). No depende del backend; el
/// backend solo declara sus `RasterCaps`.
struct RasterPolicy {
	AccelMode mode = AccelMode::Auto; ///< modo de aceleración
	eng::u16 min_blit_pixels = 64;    ///< en `Auto`, área mínima para ir al Blitter
	bool cpu_fast = true;             ///< rutas CPU de 32 bits (68020+)
};

class Rasterizer; ///< seam de rasterizado (definido en `raster.hpp`)

/// Vista de hardware que el compositor de la escena necesita para programar el
/// Copper. Es el contrato común de TODOS los playfields: BPL pointers, scroll
/// fino/coarse, modulos y, en los playfields con wrap vertical (corkscrew), el
/// split (`display_offset`, `split_line`, `split_active`).
struct PlayfieldHardwareView {
    const u8* bitplanes = nullptr; // frontbuffer (Planes[0] + bitmapoffset)
    const u8* real_base = nullptr; // base real del AllocBitMap (para BPLxPT)
    u32 planeaddx = 0;             // coarse X en bytes
    u32 planeaddy = 0;             // offset Y interleaved = display_offset*planes*bytes
    u16 bplcon1 = 0;               // scroll fino duplicado en ambos nibbles
    u16 bpl1mod = 0;               // módulo del plano 1 (bytes ignorados al saltar de fila)
    u16 bpl2mod = 0;               // módulo del plano 2 (ídem)
    u16 bitmap_bytes_per_row = 0;  // bytes por fila (ej. viewport_w+32)/8
    u32 plane_bytes = 0;           // bytes totales (para validación)
    u8 planes = 0;                 // nº de planos de bitplane
    u16 bitmap_height = 0;         // altura física total (allocation)
    u16 display_height = 0;        // bucle vertical del display (viewport_h + EXTRAHEIGHT si corkscrew)
    u16 display_offset = 0;        // yoffset = (videoposy + tile_height) % display_height
    u16 split_line = 0;            // filas dentro de la ventana donde ocurre el wrap
    bool split_active = false;     // split_line < viewport_h (hace falta Copper split)
    u32 split_planeaddy = 0;       // offset Y de los punteros del split (fila 0)
    u16 viewport_w = 0;            // ancho de la ventana visible en píxeles
    u16 viewport_h = 0;            // alto de la ventana visible en filas
    // Parallax por plano (RoboCod): si `parallax_plane < planes`, ESE plano usa
    // `parallax_planeaddx` (coarse X propio) en vez de `planeaddx`, de modo que su
    // contenido (p. ej. un patrón de fondo) scrollea a otra velocidad.
    u8 parallax_plane = 0xffu;     // plano con parallax propio (0xff = ninguno)
    u32 parallax_planeaddx = 0;    // coarse X propio del plano de parallax (bytes)
    const u8* bg_plane_base = nullptr; // base del plano de fondo si es doble-buffer
                                        // (soft DPF): ESE plano se lee de aquí, no de real_base
    s32 videoposx = 0;
    s32 mapposx = 0;
    s32 videoposy = 0;
    s32 mapposy = 0;
};

/// Seam opcional para delegar el **relleno de polígonos al hardware** (Blitter).
///
/// El `Playfield` es agnóstico del backend: guarda un puntero de función + un
/// contexto opacos que la escena/app instala con la implementación concreta (p. ej.
/// `eng::amiga::PolygonFillService`, que usa la máscara + cookie-cut del Blitter).
/// Si no hay sink, `Playfield::fill_polygon` cae al relleno CPU por scanline, de
/// modo que la misma llamada funciona en host y en cualquier modo/layout.
///
/// La geometría viaja como strides explícitos para servir a planos CONTIGUOS y
/// INTERLEAVED sin que el sink conozca el tipo de playfield:
///   - `plane_base`: inicio del plano 0 (fila 0).
///   - `plane_stride`: bytes entre el plano p y el p+1.
///   - `row_stride`: bytes entre filas consecutivas del MISMO plano.
///   - `row_bytes`: bytes por fila de UN plano (= stride de la máscara 1 bit).
///
/// NO VERIFICADA (demo descartada): validada por el test host HOST-062 y por una
/// prueba en hardware (cubo sólido por Blitter), pero la demo que lo ejercitaba se
/// retiró por coste. Ver `eng::amiga::PolygonFillService`.
struct PolygonFillSink {
    using Fn = bool (*)(void* ctx, u8* plane_base, u8 planes, u32 plane_stride,
                        u32 row_stride, u16 row_bytes, u16 bitmap_w, u16 bitmap_h,
                        const s16* xs, const s16* ys, u8 n, u8 color);
    void* ctx = nullptr;
    Fn fn = nullptr;
    constexpr bool ready() const { return fn != nullptr && ctx != nullptr; }
};

/// Base abstracta de playfield: posee el framebuffer y la geometría, y expone
/// las primitivas de dibujo (CPU y Blitter) con validación de límites. El mapeo
/// lógico→físico es un hook virtual que cada tipo concreto implementa:
///
///   - `planeline_for(wy)`  fila física (planelínea base) de la fila de mundo.
///   - `byte_for(wx)`       word byte del píxel de mundo (el *walk* horizontal
///                          puede cruzar planelíneas si `supports_walk()`).
///   - `mirror_planelines()`  desplazamiento del espejo (0 si no hay espejo).
///   - `in_bounds(wx, wy)`  rango lógico válido (por defecto: wx,wy >= 0).
///
/// Con esos hooks, `set_pixel`/`fill_rect`/`draw_line` están implementados UNA
/// vez en la base; los blits son virtuales porque la costura/espejo dependen del
/// layout concreto.
class Playfield {
public:
    Playfield() = default;
    Playfield(const Playfield&) = delete;
    Playfield& operator=(const Playfield&) = delete;
    // Sin destructor virtual: el engine no hace heap ni borra polimórficamente
    // (la Scene posee los playfields como miembros concretos). Evita que el
    // compilador emita `operator delete` (_ZdlPvm) y bloat del vtable.

    // --- Geometría --------------------------------------------------------
    constexpr u16 width() const { return m_width; }
    constexpr u16 height() const { return m_height; }
    constexpr u8 planes() const { return m_planes; }
    constexpr u16 bytes_per_row() const { return m_bytes_per_row; }
    constexpr bool initialized() const { return m_initialized; }

    // --- Hooks de mapeo lógico->físico (implementa cada tipo) -------------
    virtual u32 planeline_for(s32 wy) const = 0;
    virtual u32 byte_for(s32 wx) const = 0;
    virtual u32 mirror_planelines() const { return 0; }
    virtual bool supports_walk() const { return false; }
    virtual bool in_bounds(s32 wx, s32 wy) const { return wx >= 0 && wy >= 0; }
    virtual u32 total_bytes() const { return m_total_bytes; }

    // --- Escritura atómica vía el mapeo (lo usa `Surface`) ----------------
    /// Escribe un píxel de mundo (lo atómico del mapeo lógico→físico). Devuelve
    /// false si está fuera de rango. En playfields con espejo (linear_display)
    /// duplica al espejo. `Surface` añade el recorte (clip) por encima de esto.
    bool write_pixel(s32 wx, s32 wy, u8 color) {
        if (!m_initialized || !in_bounds(wx, wy)) return false;
        const u32 byte = byte_for(wx);
        if (!supports_walk() && byte >= m_bytes_per_row) return false;
        const u16 mask = static_cast<u16>(0x8000u >> (wx & 15));
        const u32 pl = planeline_for(wy);
        write_planes(pl, byte, mask, color);
        const u32 mir = mirror_planelines();
        if (mir != 0u) write_planes(pl + mir, byte, mask, color);
        return true;
    }

    /// Escribe un tramo horizontal `[x0, x1]` (inclusive) en la fila `wy`. Un byte completo
    /// (16 píxeles) se resuelve con una sola escritura por plano; los extremos parciales van
    /// con máscara. Es la versión rápida del bucle de `write_pixel` para el relleno de
    /// polígonos (`fill_polygon`).
    bool draw_span(s32 x0, s32 x1, s32 wy, u8 color) {
        if (!m_initialized || !in_bounds(x0, wy) || x1 < x0) return false;
        const u32 pl = planeline_for(wy);
        const u32 mir = mirror_planelines();
        for (s32 x = x0; x <= x1;) {
            const u32 bit = static_cast<u32>(x) & 15u;
            // Tramo de 32 px alineado: 2 palabras por plano en un store de 32 bits
            // (en Chip RAM cuesta ~lo mismo que uno de 16). Es lo que hace que
            // `Surface::fill_rect` rinda como el `clear_rect` optimizado a mano.
            if (bit == 0u && x + 31 <= x1) {
                const u32 wb = byte_for(x);
                if (!supports_walk() && wb >= m_bytes_per_row) break;
                write_planes32(pl, wb, 0xffffffffu, color);
                if (mir != 0u) write_planes32(pl + mir, wb, 0xffffffffu, color);
                x += 32;
                continue;
            }
            const u32 byte = byte_for(x);
            if (!supports_walk() && byte >= m_bytes_per_row) break;
            const s32 remain = static_cast<s32>(16u - bit);
            const s32 run = (x1 - x + 1 < remain) ? (x1 - x + 1) : remain;
            const u16 hi = static_cast<u16>(0xFFFFu << (16u - static_cast<u32>(run)));
            const u16 mask = static_cast<u16>(hi >> bit);
            write_planes(pl, byte, mask, color);
            if (mir != 0u) write_planes(pl + mir, byte, mask, color);
            x += run;
        }
        return true;
    }

    /// `draw_span` con **operación lógica**. `Copy`/`Clear` reutilizan la ruta rápida
    /// (`draw_span`); `Or`/`And`/`Xor` van por `write_planes_op` (palabra a palabra).
    bool draw_span_op(s32 x0, s32 x1, s32 wy, u8 color, RasterOp op) {
        if (op == RasterOp::Copy) return draw_span(x0, x1, wy, color);
        if (op == RasterOp::Clear) return draw_span(x0, x1, wy, 0u);
        if (!m_initialized || !in_bounds(x0, wy) || x1 < x0) return false;
        const u32 pl = planeline_for(wy);
        const u32 mir = mirror_planelines();
        for (s32 x = x0; x <= x1;) {
            const u32 byte = byte_for(x);
            if (!supports_walk() && byte >= m_bytes_per_row) break;
            const u32 bit = static_cast<u32>(x) & 15u;
            const s32 remain = static_cast<s32>(16u - bit);
            const s32 run = (x1 - x + 1 < remain) ? (x1 - x + 1) : remain;
            const u16 hi = static_cast<u16>(0xFFFFu << (16u - static_cast<u32>(run)));
            const u16 mask = static_cast<u16>(hi >> bit);
            write_planes_op(pl, byte, mask, color, op);
            if (mir != 0u) write_planes_op(pl + mir, byte, mask, color, op);
            x += run;
        }
        return true;
    }

    /// Copia rectangular por **CPU** (palabra a palabra) sobre los planos. Cuando
    /// `m_raster_policy.cpu_fast` está activo y origen y destino quedan alineados a 4
    /// bytes, copia de 32 en 32 (ruta 68020+: `move.l`; *CPU blit assist*, ver
    /// `docs/guides/optimization/OPTIMIZACION_GPP_68000.md`). No encola nada.
    bool copy_rect_cpu(Span<const u16> src, s32 wx, s32 wy, u16 w, u16 h,
                       u16 src_row_bytes, u32 src_plane_stride, u8 planes) {
        if (!m_initialized || src.empty() || planes == 0u) return false;
        if (wx < 0 || (wx & 15) != 0 ||
            static_cast<u32>(wx / 8) + (w / 8u) > m_bytes_per_row) {
            return false;
        }
        if (wy < 0 || static_cast<u32>(wy) + h > m_height) return false;
        const u16 words = static_cast<u16>(w / 16u);
        const u32 need_src =
            (planes > 1u ? eng::math::mulu16(static_cast<u16>(planes - 1u), static_cast<u16>(src_plane_stride / 2u)) : 0u) +
            (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u) +
            static_cast<u32>(words);
        if (src.size() < need_src) return false;
        const u16 x_byte = static_cast<u16>(wx / 8u);
        // Sin multiplicaciones de 32 bits en el bucle: `wy*m_row_stride` con `mulu16`
        // y avance por suma de punteros (evita `__mulsi3` en 68000).
        const u32 y0_off = eng::math::mulu16(static_cast<u16>(wy), static_cast<u16>(m_row_stride));
        const u8* srow0 = reinterpret_cast<const u8*>(src.data());
        u8* drow0 = m_frontbuffer + y0_off + x_byte;
        for (u8 p = 0; p < planes; ++p) {
            const u8* srow = srow0;
            u8* drow = drow0;
            for (u16 row = 0; row < h; ++row) {
                const u16* s = reinterpret_cast<const u16*>(srow);
                u16* d = reinterpret_cast<u16*>(drow);
                const bool wide = m_raster_policy.cpu_fast && words >= 2u &&
                                  (reinterpret_cast<eng::uintptr>(s) & 3u) == 0u &&
                                  (reinterpret_cast<eng::uintptr>(d) & 3u) == 0u;
                u16 i = 0;
                if (wide) {
                    for (; i + 1u < words; i += 2u) {
                        *reinterpret_cast<u32*>(d + i) = *reinterpret_cast<const u32*>(s + i);
                    }
                }
                for (; i < words; ++i) {
                    d[i] = s[i];
                }
                srow += src_row_bytes;
                drow += m_row_stride;
            }
            srow0 += src_plane_stride;
            drow0 += m_plane_stride;
        }
        return true;
    }

    /// Copia rectangular **enmascarada** (cookie-cut) por **CPU**: `D = (D & ~m) | (S & m)`
    /// palabra a palabra, con la máscara de 1 bit (misma geometría que
    /// `add_world_bitmap_masked`). No encola nada.
    bool copy_masked_cpu(Span<const u16> src, Span<const u16> mask, s32 wx, s32 wy,
                         u16 w, u16 h, u16 src_row_bytes, u32 src_plane_stride, u8 planes) {
        if (!m_initialized || src.empty() || mask.empty() || planes == 0u) return false;
        if (wx < 0 || (wx & 15) != 0 ||
            static_cast<u32>(wx / 8) + (w / 8u) > m_bytes_per_row) {
            return false;
        }
        if (wy < 0 || static_cast<u32>(wy) + h > m_height) return false;
        const u16 words = static_cast<u16>(w / 16u);
        const u32 need_src =
            (planes > 1u ? eng::math::mulu16(static_cast<u16>(planes - 1u), static_cast<u16>(src_plane_stride / 2u)) : 0u) +
            (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u) +
            static_cast<u32>(words);
        const u32 need_mask =
            (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u) +
            static_cast<u32>(words);
        if (src.size() < need_src || mask.size() < need_mask) return false;
        const u16 x_byte = static_cast<u16>(wx / 8u);
        const u32 y0_off = eng::math::mulu16(static_cast<u16>(wy), static_cast<u16>(m_row_stride));
        const u8* srow0 = reinterpret_cast<const u8*>(src.data());
        const u8* mrow0 = reinterpret_cast<const u8*>(mask.data());
        u8* drow0 = m_frontbuffer + y0_off + x_byte;
        for (u8 p = 0; p < planes; ++p) {
            const u8* srow = srow0;
            const u8* mrow = mrow0;
            u8* drow = drow0;
            for (u16 row = 0; row < h; ++row) {
                const u16* s = reinterpret_cast<const u16*>(srow);
                const u16* mrow16 = reinterpret_cast<const u16*>(mrow);
                u16* d = reinterpret_cast<u16*>(drow);
                for (u16 i = 0; i < words; ++i) {
                    const u16 m = mrow16[i];
                    d[i] = static_cast<u16>((d[i] & static_cast<u16>(~m)) | (s[i] & m));
                }
                srow += src_row_bytes;
                mrow += src_row_bytes;
                drow += m_row_stride;
            }
            srow0 += src_plane_stride;
            drow0 += m_plane_stride;
        }
        return true;
    }

    // --- Layout planar para el sink de relleno (strides) ------------------
    /// Stride entre planos consecutivos y entre filas del MISMO plano. Los
    /// playfields del engine son INTERLEAVED (una fila de cada plano seguida),
    /// así que el default es `plane_stride = row_bytes`,
    /// `row_stride = planes*row_bytes`. Un playfield con planos contiguos lo
    /// sobrescribe (`plane_stride = bytes por plano`, `row_stride = row_bytes`).
    virtual u32 plane_stride() const { return m_bytes_per_row; }
    virtual u32 row_stride() const { return static_cast<u32>(m_planes) * m_bytes_per_row; }

    /// Instala (o borra, con `{}`) el motor de **relleno por hardware** del
    /// playfield. Ver `PolygonFillSink`.
    void set_polygon_fill_sink(PolygonFillSink sink) { m_fill_sink = sink; }
    /// `true` si hay un motor de relleno por hardware instalado.
    [[nodiscard]] constexpr bool has_fill_sink() const { return m_fill_sink.ready(); }

    /// **Rasterizador** (seam CPU/Blitter) que usan las `Surface` de este playfield.
    /// `nullptr` = rasterizador CPU por defecto (lo resuelve `Surface`).
    void set_rasterizer(Rasterizer* r) { m_rasterizer = r; }
    [[nodiscard]] Rasterizer* rasterizer() const { return m_rasterizer; }
    /// Política de aceleración (modo + umbrales); ver `RasterPolicy`.
    void set_raster_policy(const RasterPolicy& p) { m_raster_policy = p; }
    [[nodiscard]] const RasterPolicy& raster_policy() const { return m_raster_policy; }

    // --- Relleno de polígono (hook; el backend puede usar Blitter) --------
    /// Rellena un polígono **convexo** (scanline even-odd, CPU) con `color`. El
    /// llamador (`Surface`) ya recortó el polígono a su clip, así que aquí solo hay
    /// que escribir en el bitmap (vía `write_pixel`, que acota a los límites).
    ///
    /// Si hay un `PolygonFillSink` instalado (p. ej. el Blitter del backend Amiga),
    /// delega en él; en caso contrario usa el relleno CPU. El llamador dibuja a
    /// través de `Surface` y no distingue la ruta.
    virtual bool fill_polygon(const s16* xs, const s16* ys, u8 n, u8 color) {
        if (xs == nullptr || ys == nullptr || n < 3u) return false;
        if (m_fill_sink.ready()) {
            return m_fill_sink.fn(m_fill_sink.ctx, m_frontbuffer, m_planes, plane_stride(),
                                  row_stride(), m_bytes_per_row, m_width, m_height, xs, ys, n, color);
        }
        // Relleno CPU por DOS CADENAS (poligono convexo): O(altura) frente a O(lados*altura)
        // del barrido que recalcula min/max por scanline. El polígono ya llega convexo.
        constexpr u8 kMaxFillVerts = 16u;
        if (n > kMaxFillVerts) return false;
        s32 x32[kMaxFillVerts];
        s32 y32[kMaxFillVerts];
        for (u8 i = 0u; i < n; ++i) {
            x32[i] = xs[i];
            y32[i] = ys[i];
        }
        eng::math3d::convex_spans(
            eng::Span<const s32>(x32, n), eng::Span<const s32>(y32, n),
            [&](s32 y, s32 xl, s32 xr) { draw_span(xl, xr, y, color); });
        return true;
    }

    // --- Blits (virtuales; la costura/espejo dependen del layout) ---------    /// Fuente y máscara viajan como `Span<const u16>`: el tamaño que el caller
    /// declara ES el contrato y el playfield lo valida antes de encolar el job
    /// (devuelve false si el origen no cubre `src_plane_stride*planes`). 
    virtual bool add_world_bitmap(graphics::FramePlan& plan, Span<const u16> src,
                                  s32 wx, s32 wy, u16 w, u16 h,
                                  u16 src_row_bytes, u32 src_plane_stride,
                                  u8 planes) = 0;
    virtual bool add_world_bitmap_masked(graphics::FramePlan& plan, Span<const u16> src,
                                         Span<const u16> mask, s32 wx, s32 wy,
                                         u16 w, u16 h, u16 src_row_bytes,
                                         u32 src_plane_stride, u8 planes) = 0;

    // --- Scroll (especialización del playfield) ---------------------------
    /// Avanza el scroll 1 px por eje (o 0). Por defecto no scrollea (lienzo).
    virtual bool update_scroll(graphics::FramePlan& plan, s32 dx, s32 dy) {
        (void)plan; (void)dx; (void)dy;
        return true;
    }
    virtual s32 mapposx() const { return 0; }
    virtual s32 mapposy() const { return 0; }
    virtual s32 videoposx() const { return 0; }
    virtual s32 videoposy() const { return 0; }

    // --- Vista hardware para el compositor --------------------------------
    virtual PlayfieldHardwareView hardware_view() const = 0;

protected:
    /// Escribe la máscara (bit del píxel) en los `planes` bitplanes interleaved
    /// de la planelínea `planeline` en la word `word_byte`. Acota contra el
    /// tamaño total del bitmap (el *walk* horizontal cruza planelíneas).
    void write_planes(u32 planeline, u32 word_byte, u16 mask, u8 color) {
        // Strides por layout: por defecto (0) se deriva el INTERLEAVED, donde
        // `planeline` ya incluye el índice de plano (`wy*planes + walk`) y cada plano
        // está a `m_bytes_per_row`. Un playfield CONTIGUO fija `m_plane_stride` y
        // `m_row_stride` en su `bind` (planos uno tras otro). El offset por plano se
        // acumula (suma) para no meter un producto de 32 bits en el camino por píxel.
        const u32 pstride = (m_plane_stride != 0u) ? m_plane_stride : m_bytes_per_row;
        const u32 rstride = (m_row_stride != 0u) ? m_row_stride : m_bytes_per_row;
        u8* base = m_frontbuffer +
                   eng::math::mulu16(static_cast<u16>(planeline), static_cast<u16>(rstride));
        u32 off = word_byte;
        for (u8 p = 0; p < m_planes; ++p) {
            if (off >= m_total_bytes) return; // fuera del bitmap
            u16* w = reinterpret_cast<u16*>(base + off);
            if ((color & (1u << p)) != 0u) *w |= mask;
            else *w &= ~mask;
            off += pstride;
        }
    }

    /// Igual que `write_planes` pero para **dos palabras contiguas** (32 px) con un solo
    /// store de 32 bits por plano. Si el destino no queda alineado a 4 bytes (posible en
    /// planos contiguos con `row_bytes` no múltiplo de 4), cae a dos stores de 16 bits
    /// (nunca un acceso desalineado, que en 68000 sería una excepción de dirección).
    void write_planes32(u32 planeline, u32 word_byte, u32 mask32, u8 color) {
        const u32 pstride = (m_plane_stride != 0u) ? m_plane_stride : m_bytes_per_row;
        const u32 rstride = (m_row_stride != 0u) ? m_row_stride : m_bytes_per_row;
        u8* base = m_frontbuffer +
                   eng::math::mulu16(static_cast<u16>(planeline), static_cast<u16>(rstride));
        u32 off = word_byte;
        for (u8 p = 0; p < m_planes; ++p) {
            if (off + 3u >= m_total_bytes) return; // fuera del bitmap
            u8* addr = base + off;
            if ((reinterpret_cast<eng::uintptr>(addr) & 3u) == 0u) {
                u32* w = reinterpret_cast<u32*>(addr);
                if ((color & (1u << p)) != 0u) *w |= mask32;
                else *w &= ~mask32;
            } else {
                u16* w = reinterpret_cast<u16*>(addr);
                const u16 lo = static_cast<u16>(mask32 & 0xffffu);
                const u16 hi = static_cast<u16>(mask32 >> 16);
                if ((color & (1u << p)) != 0u) { w[0] |= lo; w[1] |= hi; }
                else { w[0] &= static_cast<u16>(~lo); w[1] &= static_cast<u16>(~hi); }
            }
            off += pstride;
        }
    }

    /// `write_planes` con **operación lógica** (`RasterOp`): aplica `color` a la palabra
    /// con `|=`, `&=`, `^=` o `=`. Camino no caliente (la ruta `Copy` va por `draw_span`).
    void write_planes_op(u32 planeline, u32 word_byte, u16 mask, u8 color, RasterOp op) {
        const u32 pstride = (m_plane_stride != 0u) ? m_plane_stride : m_bytes_per_row;
        const u32 rstride = (m_row_stride != 0u) ? m_row_stride : m_bytes_per_row;
        u8* base = m_frontbuffer +
                   eng::math::mulu16(static_cast<u16>(planeline), static_cast<u16>(rstride));
        u32 off = word_byte;
        for (u8 p = 0; p < m_planes; ++p) {
            if (off >= m_total_bytes) return;
            u16* w = reinterpret_cast<u16*>(base + off);
            const u16 v = ((color & (1u << p)) != 0u) ? 0xffffu : 0x0000u;
            switch (op) {
                case RasterOp::Or: *w = static_cast<u16>(*w | (v & mask)); break;
                case RasterOp::And: *w = static_cast<u16>(*w & static_cast<u16>(v | static_cast<u16>(~mask))); break;
                case RasterOp::Xor: *w = static_cast<u16>(*w ^ (v & mask)); break;
                default: *w = static_cast<u16>((*w & static_cast<u16>(~mask)) | (v & mask)); break;
            }
            off += pstride;
        }
    }

    u8* m_frontbuffer = nullptr; ///< base de los bitplanes (Chip RAM) del playfield
    u16 m_width = 0;             ///< ancho visible en píxeles
    u16 m_height = 0;            ///< alto en filas
    u16 m_bytes_per_row = 0;     ///< bytes por fila de un plano
    u8 m_planes = 0;             ///< nº de planos de bitplane
    u32 m_total_bytes = 0;       ///< bytes totales del bitmap (`row * plano * planos`)
    u32 m_plane_stride = 0;      ///< bytes entre planos (0 = interleaved: `m_bytes_per_row`)
    u32 m_row_stride = 0;        ///< bytes entre filas del mismo plano (0 = `m_bytes_per_row`)
    bool m_initialized = false;  ///< el playfield quedó listo para dibujar
    PolygonFillSink m_fill_sink {}; ///< motor de relleno por hardware (vacío = CPU)
    Rasterizer* m_rasterizer = nullptr; ///< seam CPU/Blitter (nullptr = CPU por defecto)
    RasterPolicy m_raster_policy {};    ///< política de aceleración
};

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
                          u8 planes) override {
        if (!m_initialized || src.empty() || planes == 0) return false;
        if (wx < 0 || (wx & 15) != 0 || static_cast<u32>(wx / 8) + (w / 8u) > m_bytes_per_row) return false;
        if (wy < 0 || static_cast<u32>(wy) + h > m_height) return false;
        const u16 words = static_cast<u16>(w / 16u);
        const u32 need_src = (planes > 1u ? eng::math::mulu16(static_cast<u16>(planes - 1u), static_cast<u16>(src_plane_stride / 2u)) : 0u)
                           + (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u)
                           + static_cast<u32>(words);
        if (src.size() < need_src) return false;
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
                1, 0, src_plane_stride, eng::math::mulu16(m_bytes_per_row, m_planes), false
            };
            if (!plan.add_copy_rect(job)) return false;
        }
        return true;
    }

    /// BOB enmascarado en el lienzo (cookie-cut, máscara 1 bit compartida).
    bool add_world_bitmap_masked(graphics::FramePlan& plan, Span<const u16> src,
                                 Span<const u16> mask, s32 wx, s32 wy,
                                 u16 w, u16 h, u16 src_row_bytes,
                                 u32 src_plane_stride, u8 planes) override {
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
                1, 0, src_plane_stride, eng::math::mulu16(m_bytes_per_row, m_planes), false
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
        const u16 row = static_cast<u16>((width / 8u) & ~1u);
        const u32 pbytes = (plane_stride != 0u) ? plane_stride
                                                : static_cast<u32>(row) * height;
        const u32 need = pbytes * planes;
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
                          u8 planes) override {
        if (!m_initialized || src.empty() || planes == 0u) return false;
        if (wx < 0 || (wx & 15) != 0 || static_cast<u32>(wx / 8) + (w / 8u) > m_bytes_per_row) return false;
        if (wy < 0 || static_cast<u32>(wy) + h > m_height) return false;
        const u16 words = static_cast<u16>(w / 16u);
        const u32 need_src = (planes > 1u ? eng::math::mulu16(static_cast<u16>(planes - 1u), static_cast<u16>(src_plane_stride / 2u)) : 0u)
                           + (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u)
                           + static_cast<u32>(words);
        if (src.size() < need_src) return false;
        const u16 x_byte = static_cast<u16>(wx / 8u);
        const s16 src_mod = static_cast<s16>(src_row_bytes - words * 2);
        const s16 dst_mod = static_cast<s16>(m_bytes_per_row - words * 2);
        const u16* sbase = src.data();
        const u32 y0_off = eng::math::mulu16(static_cast<u16>(wy), static_cast<u16>(m_row_stride));
        const u8* sp = reinterpret_cast<const u8*>(sbase);
        u8* dp = m_frontbuffer + y0_off + x_byte;
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = reinterpret_cast<const u16*>(sp);
            u16* d = reinterpret_cast<u16*>(dp);
            graphics::BlitJob job {
                graphics::BlitJobKind::CopyRect, graphics::BlitSource {}, graphics::BlitSource {s}, graphics::BlitDest {d},
                words, h, src_mod, dst_mod,
                1, 0, src_plane_stride, m_plane_stride, false
            };
            if (!plan.add_copy_rect(job)) return false;
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
                                 u32 src_plane_stride, u8 planes) override {
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
                1, 0, src_plane_stride, m_plane_stride, false
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
