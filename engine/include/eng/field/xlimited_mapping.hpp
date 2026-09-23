#pragma once

/// \file xlimited_mapping.hpp
/// **Mapeo y geometría** del corkscrew X-Limited (`XLimitedMapping`): hooks de mapeo
/// lógico→físico, helpers de geometría (módulo del bucle, `fetch_*`, altura del bitmap) y los
/// accesores del contrato `ScrollSink`. `XLimitedPlayfield` (`xlimited_playfield.hpp`)
/// añade el scroll, los blits y el ciclo de vida. Cabecera de familia: `xlimited.hpp`.

#include <eng/field/xlimited_base.hpp>

namespace eng::field {

/// **Mapeo y geometría** del X-Limited: la parte del playfield que el `ScrollEngine` consulta
/// (contrato `ScrollSink`) y que resuelve las direcciones del layout interleaved. No tiene
/// estado de scroll ni memoria propia: la posee `XLimitedPlayfield`.
template <ScrollConsts SC = ScrollConsts{}, class MapT = TileLayerMap>
class XLimitedMapping : public Playfield {
public:
    XLimitedMapping() = default;
    XLimitedMapping(const XLimitedMapping&) = delete;
    XLimitedMapping& operator=(const XLimitedMapping&) = delete;

    /// Fila (en planelíneas) de inicio de la fila de mundo `wy` en el bucle
    /// vertical (costura del split). Hook del mapeo de la base `Playfield`.
    u32 planeline_for(s32 wy) const override {
        return static_cast<u32>(dmod2(wy)) * cplanes();
    }
    /// Word byte del píxel de mundo (el *walk* horizontal cruza planelíneas
    /// cuando `wx/8 >= bitmap_bytes_per_row`; se acota en `write_planes`).
    u32 byte_for(s32 wx) const override {
        return static_cast<u32>(wx / 8) & ~1u;
    }
    /// Espejo del modo lineal (0 si no hay espejo).
    u32 mirror_planelines() const override { return m_mirror_planelines; }
    /// El corkscrew soporta el walk horizontal (los blits y la CPU lo acotan).
    bool supports_walk() const override { return true; }

    // --- Helper de módulo por display_height ---------------------------------
    // la geometría entra como constante NTTP (SC) si se conoce; si no, runtime.
    // La doble módulo preserva el original (robusto a negativos).
    inline s32 dmod2(s32 v) const {
        if constexpr (SC.display_height != 0u) {
            const s32 dh = static_cast<s32>(SC.display_height);
            return (v % dh + dh) % dh;
        }
        const s32 dh = static_cast<s32>(m_display_height);
        return (v % dh + dh) % dh;
    }
    inline u32 dmod1(u32 v) const { // v % display_height (no-negativo)
        if constexpr (SC.display_height != 0u) return fast_div<SC.display_height>::r(v);
        return v % m_display_height;
    }

    // --- Geometría como CONSTANTE cuando `SC` la conoce ----------------------
    // `begin()` valida que SC coincida con la config. Leer SC en vez de `m_cfg.*`
    // deja que el compilador pliegue `320/tile_width`, `block%blocks_per_row`,
    // `tile_width/16`... (con `m_cfg` son división/módulo runtime → libcall en
    // 68000). Si SC no trae el valor, se cae al campo runtime (mismo resultado).
    constexpr u16 ctw() const { return SC.tile_width ? static_cast<u16>(SC.tile_width) : m_cfg.tile_width; }
    constexpr u16 cth() const { return SC.tile_height ? static_cast<u16>(SC.tile_height) : m_cfg.tile_height; }
    constexpr u8 cplanes() const { return SC.planes ? static_cast<u8>(SC.planes) : m_cfg.planes; }

    /// Calcula la altura total según la fórmula canónica de Steger parametrizada.
    ///
    /// X-Limited puro (scroll_y=false): `viewport_h + (map_width/…/planes) + 1 + 3`,
    /// igual que xlimited.c (`BITMAPHEIGHT = SCREENHEIGHT`).
    /// Corkscrew/XY (scroll_y=true): el bucle vertical del display mide
    /// `viewport_h + EXTRAHEIGHT` con `EXTRAHEIGHT = 2*tile_height` (banda de
    /// staging de 2 bloques), igual que XYLimited (`BITMAPHEIGHT =
    /// SCREENHEIGHT + EXTRAHEIGHT`); además se alinea a `tile_height` para que la
    /// última banda de staging (`block_videoposy`) quepa sin salir de Chip RAM.
    ///
    /// \param display_height  alto del bucle vertical del corkscrew (anillo; ya
    ///                        incluye el EXTRAHEIGHT de 2 bloques si scroll_y)
    /// \param tile_height  alto de bloque (cfg.tile_height, 16)
    /// \param scroll_y     true = corkscrew (XY), false = X-only
    /// \param map_width_blocks  ancho del mapa en bloques (ej. screens_x*viewport_w/tile_width)
    /// \param bitmap_blocks_per_row  BITMAPWIDTH / tile_width
    /// \param planes  profundidad (4)
    /// \return altura en píxeles (display_height + extra +1+3)
    static constexpr u16 compute_bitmap_height(u16 display_height,
                                               u16 tile_height,
                                               bool scroll_y,
                                               u16 map_width_blocks,
                                               u16 bitmap_blocks_per_row,
                                               u8 planes) {
        u16 h = static_cast<u16>(
            display_height + (map_width_blocks / bitmap_blocks_per_row / planes) + 1 + 3);
        if (scroll_y) {
            h = static_cast<u16>(((h + tile_height - 1u) / tile_height) * tile_height);
        }
        return h;
    }

    constexpr u16 map_tile_at(u16 physical_x, u16 physical_y) const {
        const s32 map_x = static_cast<s32>(physical_x) - m_cfg.visible_tile_bias_x;
        const s32 map_y = static_cast<s32>(physical_y) - m_cfg.visible_tile_bias_y;
        return m_cfg.map.tile_at(map_x, map_y);
    }

    constexpr u16 twoblockstep() const {
        // TWOBLOCKS del corkscrew (XYLimited): BITMAPBLOCKSPERROW - NUMSTEPS_Y
        //   x*2 + (tile_height - x) = bitmap_blocks_per_row  →  x = bpr - TH
        return static_cast<u16>(m_bitmap_blocks_per_row > cth()
            ? m_bitmap_blocks_per_row - cth() : 0);
    }

    constexpr u16 bitmap_bytes_per_row() const { return m_bytes_per_row; }
    constexpr u16 bitmap_width() const { return m_bitmap_width; }
    constexpr u16 bitmap_height() const { return m_bitmap_height; }
constexpr u16 bitmap_blocks_per_row() const { return m_bitmap_blocks_per_row; }
    constexpr u16 display_height() const { return m_display_height; }
    constexpr u16 display_blocks_per_col() const { return m_bitmap_blocks_per_col; }

    // --- ScrollSink (concepto de scroll_engine.hpp): geometría, límites y modo.
    // El algoritmo del corkscrew vive en `ScrollEngine` y consume estos getters
    // + add_draw/save_word/restore_saveword (los seams de este layout).
    constexpr u16 tile_width() const { return ctw(); }
    constexpr u16 tile_height() const { return cth(); }
    constexpr u16 viewport_w() const { return m_cfg.viewport_w; }
    constexpr u16 viewport_h() const { return m_cfg.viewport_h; }
    /// Acceso al accesor de tiles (p. ej. para `prefetch` de un mundo con streaming).
    MapT& map_source() { return m_cfg.map; }
    const MapT& map_source() const { return m_cfg.map; }
    constexpr u16 display_planelines() const { return m_display_planelines; }
    constexpr u16 bytes_per_row() const { return m_bytes_per_row; }
    constexpr u16 bitmap_blocks_per_col() const { return m_bitmap_blocks_per_col; }
    constexpr u16 map_width_blocks() const {
        return m_cfg.map.width ? m_cfg.map.width
            : static_cast<u16>(m_cfg.screens_x * (m_cfg.viewport_w / ctw()));
    }
    constexpr u16 map_height_blocks() const {
        return m_cfg.map.height ? m_cfg.map.height
            : static_cast<u16>(m_cfg.screens_y * (m_cfg.viewport_h / cth()));
    }
    constexpr u16 map_wrap_x() const { return m_cfg.map.wrap_x; }
    constexpr u16 map_wrap_y() const { return m_cfg.map.wrap_y; }
    constexpr bool one_direction() const { return m_cfg.direction == DirectionPolicy::OneWay; }
    /// true = corkscrew/XY: el eje Y usa anillo con banda de staging y split.
    /// Deriva de `y_mode == Ring`; lo consultan el display y `hardware_view`.
    constexpr bool scroll_y() const { return m_cfg.y_mode == AxisPolicy::Ring; }

    /// Eje X lineal acotado (sin anillo ni bandas de guarda). Lo consulta el
    /// `ScrollEngine` para mover el puntero sin creep.
    constexpr bool finite_x() const { return m_cfg.x_mode == AxisPolicy::Finite; }
    constexpr u16 block_planes_lines() const { return m_block_planes_lines; }
    constexpr bool initialized() const { return m_initialized; }
    constexpr u16 bpl1mod() const { return m_bpl1mod; }

    static constexpr u16 fetch_scroll_pixels(u8 mode) {
        // fetchinfo[].scrollpixels: 16,32,32,64
        return mode == 3 ? 64 : (mode == 0 ? 16 : 32);
    }
    static constexpr u16 fetch_bitmap_offset(u8 mode) {
        // fetchinfo[].bitmapoffset: 0,16,16,48
        return mode == 3 ? 48 : (mode == 0 ? 0 : 16);
    }
    static constexpr u16 fetch_modulo_offset(u8 mode) {
        // fetchinfo[].modulooffset: 2,4,4,8
        return mode == 3 ? 8 : (mode == 0 ? 2 : 4);
    }

protected:
    bool valid_config() const {
        if (!m_cfg.tileset || m_cfg.tileset_count == 0 || m_cfg.planes == 0 ||
            m_cfg.planes > 6) return false;
        if (m_cfg.tile_width == 0 || (m_cfg.tile_width & 15u)) return false;
        if (m_cfg.tile_height == 0 || (m_cfg.tile_height & 15u)) return false;
        if (m_cfg.viewport_w == 0 || m_cfg.viewport_h == 0) return false;
        if (m_cfg.viewport_w % m_cfg.tile_width != 0) return false;
        if (m_cfg.viewport_h % m_cfg.tile_height != 0) return false;
        if (m_cfg.bitmap_width != 0) {
            if (m_cfg.bitmap_width < m_cfg.viewport_w + m_cfg.tile_width) return false;
            if (m_cfg.bitmap_width % m_cfg.tile_width != 0) return false;
        }
        if (m_cfg.screens_x == 0 || m_cfg.screens_y == 0) return false;
        // Si el mapa no trae celdas ni dimensiones, se derivará de screens_x/y → válido
        if (!m_cfg.map.has_data() && m_cfg.map.width == 0 && m_cfg.screens_x == 0) return false;
        return true;
    }

    XlimitedConfigT<MapT> m_cfg {};
    u16 m_bitmap_width = xlimited_detail::kBitmapW32;
    u16 m_bitmap_blocks_per_row = xlimited_detail::kBlocksPerRow32;
    u16 m_block_planes_lines = 0; // recalculado en begin(): BLOCKHEIGHT*planes
    u16 m_bitmap_height = 0;      // recalculado en begin(): compute_bitmap_height(...)
    u16 m_display_height = 0;     // bucle vertical del display (corkscrew)
    u16 m_display_planelines = 0; // display_height * planes (modulus del split)
    u16 m_bitmap_blocks_per_col = 0; // BITMAPBLOCKSPERCOL = display_height / tile_height
    bool m_linear_display = false;   // display lineal (sin split): espejo del bucle
    u32 m_mirror_planelines = 0;     // desplazamiento del espejo (display_height*planes)
    u16 m_bpl1mod = 0, m_bpl2mod = 0;
};

} // namespace eng::field
