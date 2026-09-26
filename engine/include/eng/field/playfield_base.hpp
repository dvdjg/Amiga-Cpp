#pragma once

/// \file playfield_base.hpp
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
///
/// ```text
///   Playfield (base abstracta)  ── framebuffer Chip + geometría + primitivas CPU (hooks de mapeo)
///   ├─ CanvasPlayfield      lienzo plano (blits / HUD)
///   ├─ XLimitedPlayfield    corkscrew 8-way (xlimited.hpp)
///   └─ Flat / Mirror / DoubleBufferPlayfield (en sus headers)
///            │ expone
///            ▼
///   PlayfieldHardwareView   BPLxPT · scroll fino/coarse · mods · split (wrap vertical)
///            ▲
///   Surface → primitivas validadas (set_pixel/fill_rect/draw_line/draw_text/blit), SIN puntero crudo
///   Scene = compone N playfields + sprites + paletas + copperlist (nunca se accede por índice)
/// ```

#include <eng/core/math/arith.hpp>
#include <eng/core/math/arith.hpp>
#include <eng/core/data/polygon.hpp>
#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
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

/// **Minterm del Blitter** para una operación lógica de **blit** con `B = D` (fuente por
/// A): `Or`=`$FC` (`D=A|B`), `And`=`$C0` (`D=A&B`), `Xor`=`$3C` (`D=A^B`). `Copy` no usa
/// esta ruta (va por C). Ver AHRM 6 (tabla de minterms).
[[nodiscard]] constexpr eng::u8 raster_op_minterm(RasterOp op) {
	switch (op) {
		case RasterOp::Or: return 0xFCu;
		case RasterOp::And: return 0xC0u;
		case RasterOp::Xor: return 0x3Cu;
		default: return 0xF0u;
	}
}

class Rasterizer; ///< seam de rasterizado (definido en `raster.hpp`)

/// Vista de hardware que el compositor de la escena necesita para programar el
/// Copper. Es el contrato común de TODOS los playfields: BPL pointers, scroll
/// fino/coarse, modulos y, en los playfields con wrap vertical (corkscrew), el
/// split (`display_offset`, `split_line`, `split_active`).
struct PlayfieldHardwareView {
    const u8* bitplanes = nullptr; // frontbuffer (Planes[0] + bitmapoffset)
    Address<MemoryKind::Chip> real_base {}; // base real del AllocBitMap (para BPLxPT), Chip RAM
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
    Address<MemoryKind::Chip> bg_plane_base {}; // base del plano de fondo si es doble-buffer
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

/// Motor de **relleno de rectángulo axis-aligned por hardware** (Blitter D-only, minterm
/// `$FF`/`$00` por plano). Mismo contrato de strides que `PolygonFillSink`. Es la ruta
/// barata para los *fills* de UI (cajas), frente al relleno de polígono (que traza el
/// contorno y hace un area-fill). Si no hay sink, `Playfield::fill_rect_hw` cae a CPU.
struct RectFillSink {
    using Fn = bool (*)(void* ctx, u8* plane_base, u8 planes, u32 plane_stride,
                        u32 row_stride, u16 row_bytes, u16 bitmap_w, u16 bitmap_h,
                        s32 x, s32 y, u16 w, u16 h, u8 color);
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
    virtual u32 planeline_for(eng::pix wy) const = 0;
    virtual u32 byte_for(eng::pix wx) const = 0;
    virtual u32 mirror_planelines() const { return 0; }
    virtual bool supports_walk() const { return false; }
    virtual bool in_bounds(eng::pix wx, eng::pix wy) const { return wx >= 0 && wy >= 0; }
    virtual u32 total_bytes() const { return m_total_bytes; }

    // --- Escritura atómica vía el mapeo (lo usa `Surface`) ----------------
    /// Escribe un píxel de mundo (lo atómico del mapeo lógico→físico). Devuelve
    /// false si está fuera de rango. En playfields con espejo (linear_display)
    /// duplica al espejo. `Surface` añade el recorte (clip) por encima de esto.
    bool write_pixel(eng::pix wx, eng::pix wy, u8 color) {
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
    bool draw_span(eng::pix x0, eng::pix x1, eng::pix wy, u8 color) {
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
    bool draw_span_op(eng::pix x0, eng::pix x1, eng::pix wy, u8 color, RasterOp op) {
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
                       u16 src_row_bytes, u32 src_plane_stride, u8 planes,
                       u8 source_shift = 0u) {
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
            static_cast<u32>(words) + ((source_shift != 0u) ? 1u : 0u);
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
                if (source_shift == 0u) {
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
                } else {
                    const u16* s = reinterpret_cast<const u16*>(srow);
                    u16* d = reinterpret_cast<u16*>(drow);
                    u16 carry = static_cast<u16>(
                        reinterpret_cast<const u16*>(s - 1)[0] >> (16u - source_shift));
                    for (u16 i = 0; i < words; ++i) {
                        const u16 cur = static_cast<u16>(s[i] << source_shift);
                        d[i] = static_cast<u16>(cur | carry);
                        carry = static_cast<u16>(s[i] >> (16u - source_shift));
                    }
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
                         u16 w, u16 h, u16 src_row_bytes, u32 src_plane_stride, u8 planes,
                         u8 source_shift = 0u) {
        if (!m_initialized || src.empty() || mask.empty() || planes == 0u) return false;
        if (wx < 0 || (wx & 15) != 0 ||
            static_cast<u32>(wx / 8) + (w / 8u) > m_bytes_per_row) {
            return false;
        }
        if (wy < 0 || static_cast<u32>(wy) + h > m_height) return false;
        const u16 words = static_cast<u16>(w / 16u);
        // Con `source_shift != 0` se lee una palabra extra por fila (shift del barrel).
        const u32 extra = (source_shift != 0u) ? 1u : 0u;
        const u32 need_src =
            (planes > 1u ? eng::math::mulu16(static_cast<u16>(planes - 1u), static_cast<u16>(src_plane_stride / 2u)) : 0u) +
            (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u) +
            static_cast<u32>(words) + extra;
        const u32 need_mask =
            (h > 1u ? eng::math::mulu16(static_cast<u16>(h - 1u), static_cast<u16>(src_row_bytes / 2u)) : 0u) +
            static_cast<u32>(words) + extra;
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
                if (source_shift == 0u) {
                    for (u16 i = 0; i < words; ++i) {
                        const u16 m = mrow16[i];
                        d[i] = static_cast<u16>((d[i] & static_cast<u16>(~m)) | (s[i] & m));
                    }
                } else {
                    // Barrel shift de máscara y fuente: `cur = (x[i]<<sh) | (x[i-1]>>(16-sh))`.
                    const u16 inv = static_cast<u16>(16u - source_shift);
                    u16 pm = 0u, ps = 0u; // palabras previas (0 al inicio de fila)
                    for (u16 i = 0; i < words; ++i) {
                        const u16 cm = static_cast<u16>((mrow16[i] << source_shift) | (pm >> inv));
                        const u16 cs = static_cast<u16>((s[i] << source_shift) | (ps >> inv));
                        d[i] = static_cast<u16>((d[i] & static_cast<u16>(~cm)) | (cs & cm));
                        pm = mrow16[i];
                        ps = s[i];
                    }
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

    /// Instala (o borra, con `{}`) el motor de **relleno de rect por hardware**. Ver `RectFillSink`.
    void set_rect_fill_sink(RectFillSink sink) { m_rect_sink = sink; }
    /// `true` si hay un motor de relleno de rect por hardware instalado.
    [[nodiscard]] constexpr bool has_rect_fill() const { return m_rect_sink.ready(); }

    /// **Rellena un rectángulo axis-aligned** con `color`: por el sink de hardware si lo hay
    /// (Blitter D-only), o por CPU (`draw_span` por fila). El llamador (`BlitterRaster`) ya
    /// decidió la ruta; aquí solo se ejecuta.
    bool fill_rect_hw(s32 x, s32 y, u16 w, u16 h, u8 color) {
        if (!m_initialized || w == 0u || h == 0u) return false;
        if (m_rect_sink.ready()) {
            return m_rect_sink.fn(m_rect_sink.ctx, m_frontbuffer, m_planes, plane_stride(),
                                  row_stride(), m_bytes_per_row, m_width, m_height, x, y, w, h,
                                  color);
        }
        const s32 x0 = x;
        const s32 x1 = static_cast<s32>(x) + static_cast<s32>(w) - 1;
        const s32 y1 = static_cast<s32>(y) + static_cast<s32>(h) - 1;
        for (s32 row = y; row <= y1; ++row) {
            draw_span(x0, x1, row, color);
        }
        return true;
    }

    /// **Línea por Blitter** (`BLTCON1` LINE) o **EOR/ONEDOT** (`eor`): encola una
    /// `BlitJobKind::Line`/`LineEor` por plano cuyo bit de color está activo. Usa
    /// `plane_stride()`/`row_stride()`, así que sirve para contiguo e interleaved. La
    /// base del canal D (EOR) es el propio bitmap. Referencia: `blitter_line`/
    /// `blitter_line_eor` del backend.
    bool add_line(graphics::FramePlan& plan, s16 x0, s16 y0, s16 x1, s16 y1, u8 color,
                  bool eor = false) {
        if (!m_initialized || color == 0u) return false;
        const u32 pstride = plane_stride();
        const u16 rstride = static_cast<u16>(row_stride());
        const graphics::BlitJobKind kind =
            eor ? graphics::BlitJobKind::LineEor : graphics::BlitJobKind::Line;
        for (u8 p = 0; p < m_planes; ++p) {
            if ((color & (1u << p)) == 0u) continue;
            graphics::BlitJob job {};
            job.destination = graphics::BlitDest {
                reinterpret_cast<u16*>(m_frontbuffer + static_cast<u32>(p) * pstride)};
            job.line.base = graphics::BlitDest {reinterpret_cast<u16*>(m_frontbuffer)};
            job.bitplane_count = 1;
            job.line.x0 = x0;
            job.line.y0 = y0;
            job.line.x1 = x1;
            job.line.y1 = y1;
            job.line.row_bytes = rstride;
            if (!plan.add_line(job, kind)) return false;
        }
        return true;
    }

    /// **Rasterizador** (seam CPU/Blitter) que usan las `Surface` de este playfield.
    /// `nullptr` = rasterizador CPU por defecto (lo resuelve `Surface`).
    void set_rasterizer(eng::Ref<Rasterizer> r) { m_rasterizer = r; }
    [[nodiscard]] eng::Ref<Rasterizer> rasterizer() const { return m_rasterizer; }
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
    virtual bool fill_polygon(eng::Span<const s16> xs, eng::Span<const s16> ys, u8 color) {
        const u8 n = static_cast<u8>(xs.size());
        if (ys.size() != xs.size() || n < 3u) return false;
        if (m_fill_sink.ready()) {
            return m_fill_sink.fn(m_fill_sink.ctx, m_frontbuffer, m_planes, plane_stride(),
                                  row_stride(), m_bytes_per_row, m_width, m_height, xs.data(),
                                  ys.data(), n, color);
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
        }        eng::math3d::convex_spans(
            eng::Span<const s32>(x32, n), eng::Span<const s32>(y32, n),
            [&](s32 y, s32 xl, s32 xr) { draw_span(xl, xr, y, color); });
        return true;
    }

    /// Atajo para arrays de tamaño fijo: `pf.fill_polygon(xs, ys, color)` sin escribir `Span`.
    /// El nº de vértices se deduce del array (deben tener la misma longitud).
    template <eng::usize N>
    bool fill_polygon(const s16 (&xs)[N], const s16 (&ys)[N], u8 color) {
        return fill_polygon(Span<const s16>(xs, N), Span<const s16>(ys, N), color);
    }

    // --- Blits (virtuales; la costura/espejo dependen del layout) ---------    /// Fuente y máscara viajan como `Span<const u16>`: el tamaño que el caller
    /// declara ES el contrato y el playfield lo valida antes de encolar el job
    /// (devuelve false si el origen no cubre `src_plane_stride*planes`). 
    virtual bool add_world_bitmap(graphics::FramePlan& plan, Span<const u16> src,
                                  s32 wx, s32 wy, u16 w, u16 h,
                                  u16 src_row_bytes, u32 src_plane_stride,
                                  u8 planes, u8 source_shift = 0u,
                                  bool descending = false,
                                  RasterOp op = RasterOp::Copy) = 0;
    virtual bool add_world_bitmap_masked(graphics::FramePlan& plan, Span<const u16> src,
                                         Span<const u16> mask, s32 wx, s32 wy,
                                         u16 w, u16 h, u16 src_row_bytes,
                                         u32 src_plane_stride, u8 planes,
                                         u8 source_shift = 0u) = 0;

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
    RectFillSink m_rect_sink {}; ///< motor de relleno de rect por hardware (vacío = CPU)
    eng::Ref<Rasterizer> m_rasterizer {}; ///< seam CPU/Blitter (vacío = CPU por defecto)
    RasterPolicy m_raster_policy {};    ///< política de aceleración
};

} // namespace eng::field
