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
/// pasa por las primitivas (que devuelven `bool` y validan); nunca se escribe a
/// `frontbuffer()` a ciegas.

#include <eng/core/types.hpp>
#include <eng/graphics/bitmap.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

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
    u16 bpl1mod = 0;
    u16 bpl2mod = 0;
    u16 bitmap_bytes_per_row = 0;  // bytes por fila (ej. viewport_w+32)/8
    u32 plane_bytes = 0;           // bytes totales (para validación)
    u8 planes = 0;
    u16 bitmap_height = 0;         // altura física total (allocation)
    u16 display_height = 0;        // bucle vertical del display (viewport_h + EXTRAHEIGHT si corkscrew)
    u16 display_offset = 0;        // yoffset = (videoposy + tile_height) % display_height
    u16 split_line = 0;            // filas dentro de la ventana donde ocurre el wrap
    bool split_active = false;     // split_line < viewport_h (hace falta Copper split)
    u32 split_planeaddy = 0;       // offset Y de los punteros del split (fila 0)
    u16 viewport_w = 0;
    u16 viewport_h = 0;
    s32 videoposx = 0;
    s32 mapposx = 0;
    s32 videoposy = 0;
    s32 mapposy = 0;
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
    constexpr const u8* frontbuffer() const { return m_frontbuffer; }
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

    // --- Blits (virtuales; la costura/espejo dependen del layout) ---------
    virtual bool add_world_bitmap(graphics::FramePlan& plan, const u16* src,
                                  s32 wx, s32 wy, u16 w, u16 h,
                                  u16 src_row_bytes, u32 src_plane_stride,
                                  u8 planes) = 0;
    virtual bool add_world_bitmap_masked(graphics::FramePlan& plan, const u16* src,
                                         const u16* mask, s32 wx, s32 wy,
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
        const u32 row = static_cast<u32>(m_bytes_per_row);
        u8* base = m_frontbuffer + planeline * row;
        for (u8 p = 0; p < m_planes; ++p) {
            const u32 b = static_cast<u32>(p) * row + word_byte;
            if (b >= m_total_bytes) return; // fuera del bitmap
            u16* w = reinterpret_cast<u16*>(base + b);
            if ((color & (1u << p)) != 0u) *w |= mask;
            else *w &= ~mask;
        }
    }

    u8* m_frontbuffer = nullptr;
    u16 m_width = 0;
    u16 m_height = 0;
    u16 m_bytes_per_row = 0;
    u8 m_planes = 0;
    u32 m_total_bytes = 0;
    bool m_initialized = false;
};

/// Lienzo plano: un playfield SIN tiles ni scroll, para blits y primitivas de
/// CPU. Es la base de un HUD, de un fondo estático o de una capa de actores.
/// Layout interleaved (`frontbuffer + planelínea*row + p*row`), sin walk, sin
/// espejo y sin costura: cada píxel vive en su fila.
class CanvasPlayfield : public Playfield {
public:
    struct Config {
        u16 width = 320;      // en píxeles
        u16 height = 32;      // p. ej. franja HUD
        u8 planes = 4;
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

    // --- Hooks (layout plano) ---------------------------------------------
    u32 planeline_for(s32 wy) const override {
        return static_cast<u32>(wy) * m_planes;
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
        // Interleaved: BPL1MOD = row*planes - fetch(viewport_w/8) - modulo_offset(2).
        v.bpl1mod = static_cast<u16>(static_cast<u32>(m_bytes_per_row) * m_planes - (m_width / 8u) - 2u);
        v.bpl2mod = v.bpl1mod;
        return v;
    }

    /// El bitmap que posee este lienzo (memoria + layout).
    const gfx::Bitmap& bitmap() const { return m_bitmap; }
    gfx::Bitmap& bitmap() { return m_bitmap; }

    /// Blit planar en el lienzo (coordenadas de lienzo = fila/columna directas,
    /// sin walk ni costura). `wx` múltiplo de 16, origen en Chip RAM.
    bool add_world_bitmap(graphics::FramePlan& plan, const u16* src,
                          s32 wx, s32 wy, u16 w, u16 h,
                          u16 src_row_bytes, u32 src_plane_stride,
                          u8 planes) override {
        if (!m_initialized || src == nullptr || planes == 0) return false;
        if (wx < 0 || (wx & 15) != 0 || static_cast<u32>(wx / 8) + (w / 8u) > m_bytes_per_row) return false;
        if (wy < 0 || static_cast<u32>(wy) + h > m_height) return false;
        const u16 words = static_cast<u16>(w / 16u);
        const u16 x_byte = static_cast<u16>(wx / 8u);
        const u32 pl = static_cast<u32>(wy) * m_planes;
        const s16 src_mod = static_cast<s16>(src_row_bytes - words * 2);
        const s16 dst_mod = static_cast<s16>(m_bytes_per_row * m_planes - words * 2);
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = src + static_cast<u32>(p) * (src_plane_stride / 2u);
            u16* d = reinterpret_cast<u16*>(m_frontbuffer + (pl + static_cast<u32>(p)) * m_bytes_per_row + x_byte);
            graphics::BlitJob job {
                graphics::BlitJobKind::CopyRect, nullptr, s, d,
                words, h, src_mod, dst_mod,
                1, 0, src_plane_stride, static_cast<u32>(m_bytes_per_row * m_planes), false
            };
            if (!plan.add_copy_rect(job)) return false;
        }
        return true;
    }

    /// BOB enmascarado en el lienzo (cookie-cut, máscara 1 bit compartida).
    bool add_world_bitmap_masked(graphics::FramePlan& plan, const u16* src,
                                 const u16* mask, s32 wx, s32 wy,
                                 u16 w, u16 h, u16 src_row_bytes,
                                 u32 src_plane_stride, u8 planes) override {
        if (!m_initialized || src == nullptr || mask == nullptr || planes == 0) return false;
        if (wx < 0 || (wx & 15) != 0 || static_cast<u32>(wx / 8) + (w / 8u) > m_bytes_per_row) return false;
        if (wy < 0 || static_cast<u32>(wy) + h > m_height) return false;
        const u16 words = static_cast<u16>(w / 16u);
        const u16 x_byte = static_cast<u16>(wx / 8u);
        const u32 pl = static_cast<u32>(wy) * m_planes;
        const s16 src_mod = static_cast<s16>(src_row_bytes - words * 2);
        const s16 dst_mod = static_cast<s16>(m_bytes_per_row * m_planes - words * 2);
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = src + static_cast<u32>(p) * (src_plane_stride / 2u);
            u16* d = reinterpret_cast<u16*>(m_frontbuffer + (pl + static_cast<u32>(p)) * m_bytes_per_row + x_byte);
            graphics::BlitJob job {
                graphics::BlitJobKind::MaskedBobCookieCut, mask, s, d,
                words, h, src_mod, dst_mod,
                1, 0, src_plane_stride, static_cast<u32>(m_bytes_per_row * m_planes), false
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
        m_frontbuffer = const_cast<u8*>(m_bitmap.frontbuffer());
    }
    gfx::Bitmap m_bitmap {};
};

} // namespace eng::field
