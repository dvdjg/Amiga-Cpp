#pragma once

/// \file xlimited_playfield.hpp
/// `XLimitedPlayfield`: corkscrew 8-way con bitmap interleaved y wrap vertical. Definido
/// aparte de `xlimited_base.hpp`; `xlimited.hpp` es la cabecera de familia.

#include <eng/field/actor_target.hpp>
#include <eng/field/tile_animation.hpp>

#include <eng/field/tile_layer_stack.hpp>
#include <eng/field/xlimited_base.hpp>
#include <eng/field/xlimited_mapping.hpp>

namespace eng::playfield {

/// Campo XLimited: scroll infinito en X con bitmap interleaved y wrap vertical.
///
/// Es una ESPECIALIZACIÓN de `Playfield`: implementa el mapeo lógico→físico del
/// corkscrew (planelínea del bucle, walk horizontal, espejo del modo lineal) y
/// la variante de scroll (8-way, X-only según `this->scroll_y`). Las primitivas de
/// dibujo CPU viven en la base `Playfield` (vía los hooks de mapeo); aquí se
/// conservan los blits (costura + espejo) y los 4 scrolls fieles a
/// `Scroller_XYLimited/main.c`.
///
/// Mantiene `mapposx`/`videoposx` como en el original y expone `draw_block`
/// con `y` en planeline. El allocation simula `BMF_INTERLEAVED`:
///
///   total_bytes = bitmap_bytes_per_row * bitmap_height * planes
///   frontbuffer = base + bitmapoffset
///   addressing  = frontbuffer + y*bitmap_bytes_per_row + x   (y = planeline)
///
/// No comparte `surface_origin` ni bandas con `TileFieldController`; sólo
/// comparte `TileLayerMap` para resolver `tile_at`.
///
/// Plantilla sobre `ScrollConsts` (NTTP): si la geometría caliente (tile
/// width/height, display_height/planelines, planes) se pasa como constante a
/// priori, el `ScrollEngine` usa `fast_div` (shifts/máscaras/multiplicación
/// mágica) y NO paga `__udivsi3` por frame; con `ScrollConsts{}` (0=todo
/// runtime) se obtiene la geometría del sink en ejecución (fallback correcto,
/// pero con divisiones). `begin()` valida que los campos de `SC` coincidan
/// con `cfg` (fallo temprano si se instancia con constantes equivocadas).
template <ScrollConsts SC = ScrollConsts{}, class MapT = TileLayerMap, class Profile = ScrollProgressive>
class XLimitedPlayfield : public XLimitedMapping<SC, MapT> {
public:
    XLimitedPlayfield() = default;


    /// Scroll de N píxeles por eje (especialización del playfield). Devuelve false
    /// si un borde del mapa bloqueó el avance (dirección inversa sin recorrido).
    ///
    /// `max_step` es el SALTO máximo configurable por eje y frame. El avance se
    /// ejecuta como `|dx|`+`|dy|` sub-pasos ATÓMICOS de 1 px: cada uno pinta su
    /// columna/fila entrante ANTES de avanzar `videopos` (paint-then-advance) y
    /// el reveal lo hace el Copper tras ejecutar el plan → NUNCA se muestra un
    /// píxel sin pintar, para cualquier salto ≤ max_step (16 px = la columna
    /// completa cada frame; el coste de Blitter crece ∝ salto).
    /// \param plan   plan del frame (recibe los blits de tira).
    /// \param dx,dy  desplazamiento por eje (recortado a `±max_step`).
    /// \return `false` si la capa no se inicializó o un borde bloqueó el avance.
    bool update_scroll(graphics::FramePlan& plan, s32 dx, s32 dy) override {
        if (!this->m_initialized) return false;
        // Perfil con `prefill`: la cámara avanza por TILES completos. Garantiza
        // que el cambio de dirección caiga en frontera de tile (`direction_latched`)
        // y que la franja entre alineada (plane-shift 0 al terminar). El perfil
        // progresivo conserva el paso exacto de la config (1 px/sub-paso).
        if constexpr (Profile::prefill) {
            dx = eng::playfield::snap_to_tiles(dx, this->ctw());
            dy = eng::playfield::snap_to_tiles(dy, this->cth());
        }
        const s32 lim = m_max_step;
        if (dx > lim) dx = lim; else if (dx < -lim) dx = -lim;
        if (dy > lim) dy = lim; else if (dy < -lim) dy = -lim;
        if (dx > 0) {
            // Perfil rápido: avance en ráfaga (geometría del cruce una sola vez).
            // El resto de ejes/direcciones conservan los sub-pasos de 1 px.
            if constexpr (Profile::prefill) {
                const s32 twv = static_cast<s32>(this->ctw());
                if (twv > 0 && (dx % twv) == 0) {
                    if (!m_scroll.burst_right(plan, *this, static_cast<u8>(dx / twv))) return false;
                } else {
                    for (s32 i = 0; i < dx; ++i) { if (!m_scroll.scroll_right(plan, *this)) return false; }
                }
            } else if constexpr (Profile::sub_px != 0u) {
                // Sub-tile: un px-burst (geometría del cruce calculada una vez por tile).
                if (!m_scroll.burst_right_px(plan, *this, static_cast<u16>(dx))) return false;
            } else {
                // Aun en el perfil progresivo, la geometría del cruce se calcula
                // una vez por ráfaga (equivalente a N pasos, HOST-034).
                if (!m_scroll.burst_right_px(plan, *this, static_cast<u16>(dx))) return false;
            }
        } else if (dx < 0) for (s32 i = 0; i < -dx; ++i) { if (!m_scroll.scroll_left(plan, *this)) return false; }
        if (dy > 0) for (s32 i = 0; i < dy; ++i) { if (!m_scroll.scroll_down(plan, *this)) return false; }
        else if (dy < 0) for (s32 i = 0; i < -dy; ++i) { if (!m_scroll.scroll_up(plan, *this)) return false; }
        return true;
    }

    /// Salto máximo (px/frame por eje) configurable. 1 = comportamiento 1px clásico.
    constexpr void set_scroll_step(u8 v) { m_max_step = v; }
    constexpr u8 scroll_step() const { return m_max_step; }

    /// Perfil de scroll estático elegido (ver `scroll_profile.hpp`).
    using scroll_profile = Profile;
    static constexpr u8 profile_fill_tiles() { return Profile::fill_tiles; }
    static constexpr u8 profile_guard_tiles() { return Profile::guard_tiles; }
    static constexpr bool profile_prefill() { return Profile::prefill; }
    static constexpr bool profile_direction_latched() { return Profile::direction_latched; }


    /// Reserva el bitmap interleaved y prepara el estado inicial.
    ///
    /// Reserva `bitmap_bytes_per_row * bitmap_height * planes` bytes en Chip
    /// RAM con alineación 16 (como `AllocBitMap(..., BMF_INTERLEAVED|BMF_CLEAR)`).
    /// `frontbuffer` apunta a `base + bitmapoffset` para los modos de fetch
    /// ancho (16 bytes para BPL32, 48 para 4x). En modo normal offset=0.
    /// \param memory  gestor de memoria (reserva Chip del bitmap interleaved).
    /// \param cfg     config de la capa (mapa, geometría, fetch, DPF…).
    /// \return `false` si la reserva o la geometría no cuadran.
    bool begin(MemoryManager& memory, const XlimitedConfigT<MapT>& cfg) {
        // Verifica en compile-time que este playfield cumple el contrato del
        // algoritmo (`ScrollEngine`); hace el scroll portátil y explícito.
        static_assert(eng::playfield::ScrollSink<XLimitedPlayfield<SC, MapT, Profile>>,
            "XLimitedPlayfield debe cumplir el sink del ScrollEngine (corkscrew/XYLimited).");
        this->m_cfg = cfg;
        m_max_step = this->m_cfg.max_step ? this->m_cfg.max_step : 1; // salto configurable (≥1)
        // Perfil estático: si impone paso (perfiles rápidos o sub-tile), su valor gana; la
        // selección se hace con un tipo (ver `scroll_profile.hpp`/`FAST_SCROLL.md`).
        if constexpr (Profile::fill_tiles != 0u || Profile::sub_px != 0u) {
            m_max_step = static_cast<u8>(Profile::max_step_px(this->m_cfg.tile_width));
        }
        // El eje Y se controla con `y_mode` (Off = X-only, sin banda de staging ni
        // split).
        if (!this->valid_config()) return false;

        // Derivar bitmap_width si es 0: viewport_w + EXTRAWIDTH según fetch_mode.
        // Con X `Finite` el bitmap contiene TODO el ancho de mundo (+ margen de
        // fetch): no hay anillo ni guardas laterales. El perfil puede pedir una
        // guarda MÁS ancha (pre-pintado por delante) que gana sobre el fetch.
        if (this->m_cfg.bitmap_width == 0) {
            u16 extra = (this->m_cfg.fetch_mode == 0) ? xlimited_detail::kExtraW32 : xlimited_detail::kExtraW64;
            if constexpr (Profile::guard_tiles != 0u) {
                const u16 guard_w = static_cast<u16>(Profile::guard_px(this->m_cfg.tile_width));
                if (guard_w > extra) extra = guard_w;
            }
            if (this->m_cfg.x_mode == AxisPolicy::Finite && this->m_cfg.map.width != 0) {
                const u16 world_w = static_cast<u16>(this->m_cfg.map.width * this->m_cfg.tile_width);
                this->m_cfg.bitmap_width = static_cast<u16>(world_w + extra);
            } else {
                this->m_cfg.bitmap_width = static_cast<u16>(this->m_cfg.viewport_w + extra);
            }
        }
        this->m_bitmap_width = this->m_cfg.bitmap_width;
        this->m_bytes_per_row = static_cast<u16>(this->m_bitmap_width / 8u);
        this->m_bitmap_blocks_per_row = static_cast<u16>(this->m_bitmap_width / this->m_cfg.tile_width);
        this->m_block_planes_lines = static_cast<u16>(this->m_cfg.tile_height * this->m_cfg.planes);
        // Derivar map_w/h si no vienen dados: screens_x/y * (viewport/tile)
        const u16 derived_map_w = static_cast<u16>(this->m_cfg.screens_x * (this->m_cfg.viewport_w / this->m_cfg.tile_width));
        const u16 derived_map_h = static_cast<u16>(this->m_cfg.screens_y * (this->m_cfg.viewport_h / this->m_cfg.tile_height));
        const u16 map_w_blocks = this->m_cfg.map.width ? this->m_cfg.map.width
                               : (this->m_cfg.map.wrap_x ? static_cast<u16>(this->m_cfg.map.wrap_x) : derived_map_w);
        // map_h no afecta a bitmap_height en X-Limited puro, pero se valida para this->scroll_y
        (void)derived_map_h;
        // Bucle vertical del display (corkscrew): display_height es el ANILLO
        // que el display recorre. Por defecto viewport_h + staging (2 bloques en
        // el perfil clásico; `guard_tiles` en un perfil rápido, para pre-pintar
        // varias filas por delante). `cfg.display_height` permite un anillo MAYOR
        // que el alto visible (p. ej. con HUD: visible 208, anillo 256+32=288)
        // para que el walk plane-shifted del scroll horizontal no colisione `mapy`.
        this->m_display_height = this->m_cfg.display_height ? this->m_cfg.display_height : static_cast<u16>(
            this->m_cfg.viewport_h + (this->scroll_y()
                ? static_cast<u16>(Profile::y_staging_tiles() * this->m_cfg.tile_height) : 0));
        this->m_display_planelines = static_cast<u16>(this->m_display_height * this->m_cfg.planes);

        this->m_bitmap_height = this->compute_bitmap_height(
            this->m_display_height, this->m_cfg.tile_height, this->scroll_y(),
            map_w_blocks,
            this->m_bitmap_blocks_per_row, this->m_cfg.planes);
        // Altura mínima: max(display_height+1+3, 16*tile_height) para que los
        // valores de mapy quepan sin que el blit desborde el bitmap.
        const u16 min_h_viewport = static_cast<u16>(this->m_display_height + 1 + 3);
        const u16 min_h_blocks = static_cast<u16>(16u * this->m_cfg.tile_height);
        const u16 min_h = (min_h_viewport > min_h_blocks) ? min_h_viewport : min_h_blocks;
        if (this->m_bitmap_height < min_h) this->m_bitmap_height = min_h;

        // Guard de `ScrollConsts` (NTTP): si la escena instancia este playfield
        // con constantes a priori, deben coincidir con el cfg; si no, el
        // ScrollEngine usaría geometría equivocada (desincronización silenciosa).
        if (SC.tile_width != 0u && SC.tile_width != this->m_cfg.tile_width) return false;
        if (SC.tile_height != 0u && SC.tile_height != this->m_cfg.tile_height) return false;
        if (SC.planes != 0u && SC.planes != this->m_cfg.planes) return false;
        if (SC.display_height != 0u && SC.display_height != this->m_display_height) return false;
        if (SC.display_planelines != 0u && SC.display_planelines != this->m_display_planelines) return false;
        // BITMAPBLOCKSPERCOL del corkscrew: filas de bloque del bucle vertical.
        this->m_bitmap_blocks_per_col = static_cast<u16>(this->m_display_height / this->m_cfg.tile_height);

        // Modo display lineal (sin split): se añade un ESpejo del bucle (filas
        // display_height..2*display_height = copia de 0..display_height). El
        // display lee de forma contigua display_offset..display_offset+viewport_h
        // y, al cruzar el final del bucle, continúa por el espejo (que se mantiene
        // en sincronía con cada blit). Esto elimina el split vertical y su
        // limitación del comparador de 8 bits (raster 256..296).
        this->m_linear_display = this->m_cfg.linear_display;
        this->m_mirror_planelines = static_cast<u32>(this->m_display_height) * this->m_cfg.planes;
        if (this->m_linear_display) {
            this->m_bitmap_height = static_cast<u16>(this->m_bitmap_height + this->m_display_height);
        }

// BMF_INTERLEAVED-like: la memoria la posee un `Bitmap` (capa de
        // memoria del engine). Modela la guardia de +64 y el offset de fetch
        // ancho (0/16/48 para BPL32/4x) que el corkscrew necesita: el bloque
        // reservado mide total_bytes+64 y `frontbuffer` = base + offset.
        gfx::BitmapConfig bc;
        bc.width = this->m_bitmap_width;
        bc.height = this->m_bitmap_height;
        bc.planes = this->m_cfg.planes;
        bc.row_bytes = this->m_bytes_per_row;
        bc.layout = gfx::PlaneLayout::Interleaved;
        bc.alignment = 16;
        bc.frontbase_offset = this->fetch_bitmap_offset(this->m_cfg.fetch_mode);
        bc.guard_bytes = 64u;
        if (!m_bitmap.init(memory, bc)) return false;
        this->m_total_bytes = m_bitmap.total_bytes();
        m_real_base = m_bitmap.allocation_start(); // base del bloque (BPLxPT)
        this->m_frontbuffer = m_bitmap.front(); // vía cruda interna (núcleo)
        // Soft DPF (RoboCod): configurar la composición y enlazar el bitmap
        // principal; si está activa reserva UN bitmap extra (doble buffer del plano
        // de fondo). Ver `soft_dpf.hpp`.
        m_soft_dpf.configure({this->m_bytes_per_row, this->m_display_height, this->m_cfg.planes, this->m_cfg.parallax_plane});
        if (!m_soft_dpf.init(memory, bc, m_bitmap)) return false;

        // BPLMODs: BITMAPBYTESPERROW*planes - SCREENBYTESPERROW - modulo_offset
        // modulo_offset = 2 (normal), 4 (BPL32/BPAGEM), 8 (BPL32+BPAGEM) según fetch_mode
        // SCREENBYTESPERROW = cfg.viewport_w / 8
        const u16 modulo_offset = this->fetch_modulo_offset(this->m_cfg.fetch_mode);
        const s32 mod = static_cast<s32>(this->m_bytes_per_row) * this->m_cfg.planes -
                        (this->m_cfg.viewport_w / 8) - modulo_offset;
        this->m_bpl1mod = static_cast<u16>(mod);
        this->m_bpl2mod = static_cast<u16>(mod);

        m_scroll.state().mapposx = 0;
        m_scroll.state().videoposx = 0;
        m_scroll.state().mapposy = 0;
        m_scroll.state().videoposy = 0;
m_scroll.state().previous_xdirection = 0; // DIRECTION_IGNORE (0=ignore, 1=left, 2=right)
        m_savewordpointer = nullptr;
        m_saveword = 0;
        m_blocks_buffer = reinterpret_cast<const u8*>(this->m_cfg.tileset);
        // Sincronizar los campos de la base `Playfield` (usados por las
        // primitivas CPU y los getters de geometría).
        this->m_planes = this->m_cfg.planes;
        this->m_width = this->m_bitmap_width;
        this->m_height = this->m_bitmap_height;
        // Patrón del plano de parallax (RoboCod) pintado una vez (CPU).
        fill_parallax_pattern();
        this->m_initialized = true;
        m_anim_target = make_actor_target(hardware_view());
        return true;
    }

    /// Rellena la pantalla inicial (equivalente a FillScreen).
    ///
    /// Emite `BITMAPBLOCKSPERROW * colHeight` jobs de Blitter en el
    /// `FramePlan` dado, donde colHeight = visibleRows + (this->scroll_y ? 2 : 0)
    /// (el corkscrew pre-rellena el bucle vertical completo de display,
    /// viewport_h + EXTRAHEIGHT, para que la banda de staging de 2 bloques
    /// tenga contenido coherente desde el primer frame).

    /// Rellena el plano de parallax (`cfg.parallax_plane`) con un patrón de
    /// "tiles" geométrico procedural (bandas diagonales + rombos, periodo 32x32).
    /// El scroll de ese plano lo da su `BPLxPT` (no se repinta por frame). Sustituir
    /// por un tileset artístico es cambiar esta función.
    void fill_parallax_pattern() {
        eng::playfield::fill_parallax_pattern(this->m_frontbuffer.ptr(), this->m_bytes_per_row, this->m_cfg.planes,
                                          this->m_cfg.parallax_plane, this->m_bitmap_width, this->m_bitmap_height);
    }

    /// Técnica **"soft DPF"** (RoboCod), REUTILIZABLE para cualquier playfield y
    /// profundidad: copia por Blitter UN rectángulo de la ventana del patrón de
    /// fondo de 1 bit al plano `this->m_cfg.parallax_plane` del bitmap. El display lee
    /// ese plano con el puntero **normal** (compartido con el FG), de modo que el
    /// movimiento del fondo lo da el CONTENIDO (la ventana), no el puntero.
    ///
    /// `src_x_pixels` es el offset de la ventana **en píxeles** dentro del patrón:
    /// el píxel 0 del destino muestra el píxel `src_x_pixels`. La parte no múltiplo
    /// de 16 la resuelve el barrel shifter A del Blitter (`source_shift`, AHRM 6,
    /// "Copying Arbitrary Regions"): modo ascendente `destino[d] = patrón[q+d-S]`
    /// con `q = src_x + S`, `S = (-src_x)&15` (word alineada). El primer `S` píxeles
    /// de cada fila llegan del shift-in y quedan a cero (se enmascara la última word
    /// con `BLTALWM`): forman una **guarda de hasta 15 px** al principio del bitmap
    /// que el llamador debe mantener fuera de la ventana (cámara X >= 16).
    ///
    /// `src_y` es la fila del patrón; `dest_row` la fila de display destino y `rows`
    /// la altura del rectángulo. `dest_byte_off`/`words` permiten copiar solo una
    /// VENTANA de la fila (p. ej. el ancho visible + 1 word de guarda) en vez de la
    /// fila completa: reduce el Blitter y permite que el blit de filas visibles quepa
    /// en el blanking vertical (§3.3). El paso entre filas del destino es `planes*bytes`.
    ///
    /// **Copper split (corkscrew)**: la ventana visible del playfield son DOS trozos
    /// del bitmap (arriba `[display_offset, display_height)`, abajo `[0, viewport-split)`).
    /// Un solo rect continuo dejaría el fondo discontinuo en el corte. El llamador
    /// emite DOS rects con el mismo `src_x`: el superior con `src_y = bg_y`, y el
    /// inferior con `src_y = bg_y + split` (las filas del patrón contiguas), de forma
    /// que el fondo queda continuo y fijo aunque el FG haga wrap vertical (§3).
    graphics::BlitJob make_bg_plane_copy_rect_job(eng::Pattern pattern, u16 pattern_row_bytes,
                                                  u16 src_x_pixels, u16 src_y,
                                                  u16 dest_row, u16 rows,
                                                  u16 dest_byte_off, u16 words) const {
        return m_soft_dpf.make_copy_rect_job(pattern, pattern_row_bytes, src_x_pixels, src_y,
                                             dest_row, rows, dest_byte_off, words);
    }

    /// Compatibilidad: copia la fila completa del anillo (`display_height` filas).
    /// \param pattern            patrón de fondo (tile/lineal).
    /// \param pattern_row_bytes  bytes por fila del patrón.
    /// \param src_x_pixels,src_y origen en el patrón (en px/filas).
    /// \return el `BlitJob` de copia de fondo.
    graphics::BlitJob make_bg_plane_copy_job(eng::Pattern pattern, u16 pattern_row_bytes,
                                             u16 src_x_pixels, u16 src_y) const {
        return m_soft_dpf.make_copy_job(pattern, pattern_row_bytes, src_x_pixels, src_y);
    }

    /// Soft DPF: conmuta el buffer de fondo delantero/trasero. Llamar TRAS escribir
    /// el blit de fondo (que va al buffer trasero) y ANTES de `compose()`.
    void bg_flip() { m_soft_dpf.flip(); }
    constexpr bool bg_double_buffered() const { return m_soft_dpf.double_buffered(); }

    /// Rellena la pantalla completa del anillo (blit de fondo por plano) en `plan`.
    /// \param plan  plan del frame.
    /// \return `false` si la capa no está inicializada.
    bool fill_screen(graphics::FramePlan& plan) const {
        if (!this->m_initialized) return false;
        const u16 cols = this->m_bitmap_blocks_per_row;
        const u16 visibleRows = static_cast<u16>(this->m_cfg.viewport_h / this->cth());
        const u16 colHeight = this->scroll_y() ? this->m_bitmap_blocks_per_col : visibleRows;
        const u16 rows = colHeight;
        for (u16 b = 0; b < rows; ++b) {
            for (u16 a = 0; a < cols; ++a) {
                if (this->m_cfg.map.is_empty(this->map_tile_at(a, b))) continue; // vacío: no pintar
                const u16 x = a * this->ctw();
                const u16 y = b * this->m_block_planes_lines; // planeline
                const u16 mapx = a;
                const u16 mapy = b;
                if (!add_draw(plan, x, y, mapx, mapy)) return false;
            }
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // Primitivas de dibujo: `set_pixel`/`fill_rect`/`draw_line` (CPU) viven en
    // la base `Playfield` e implementan el mapeo vía los hooks de arriba
    // (`planeline_for`/`byte_for`/`mirror_planelines`). Aquí se conservan los
    // blits (`add_world_bitmap`/`add_world_bitmap_masked`) porque la costura del
    // split y el espejo del modo lineal son específicos del corkscrew. Todas
    // devuelven bool y validan límites.
    // -------------------------------------------------------------------------

    /// Construye el `BlitJob` de un bloque (contrato de xlimited.c:201).
    ///
    /// \param x  coordenada X en píxeles (será word-aligned)
    /// \param y  coordenada Y en **planeline** (no píxeles)
    /// \param mapx índice de bloque en X del mapa
    /// \param mapy índice de bloque en Y del mapa
graphics::BlitJob draw_block_job(u16 x, u16 y, u16 mapx, u16 mapy) const {
        // x word-aligned como en DrawBlock: (x/8) & 0xFFFE
        const u16 x_word = static_cast<u16>((x / 8u) & 0xFFFEu);
        const u32 dst_offset = static_cast<u32>(y) * this->m_bytes_per_row + x_word;

        // Resolución del bloque del mapa (wrapping si el mapa es circular)
        const u16 block = this->map_tile_at(mapx, mapy);
        // Layout del banco de bloques: el banco X-Limited es SIEMPRE de 320 px de
        // ancho (40 B/planelínea), con `320/tile_width` bloques por fila y cada
        // tile de `tile_width/8` bytes por planelínea. Antes estaba fijado a 20
        // bloques/fila y words de 16 px (solo tiles de 16); así se soportan tiles
        // de 32×32 (10 bloques por fila, 2 words por tile).
        const u16 blocks_per_row_src = static_cast<u16>(320u / this->ctw());
        const u16 src_bytes_per_row = 40;  // BLOCKSWIDTH/8 (320/8)
        const u16 words_per_block = static_cast<u16>(this->ctw() / xlimited_detail::kBlock);
        const u32 src_row = static_cast<u32>(block / blocks_per_row_src) *
                            static_cast<u32>(this->m_block_planes_lines) * src_bytes_per_row;
        const u32 src_col = static_cast<u32>(block % blocks_per_row_src) *
                            static_cast<u32>(words_per_block * 2u);
        const u32 src_offset = src_row + src_col;

        const u8* src = m_blocks_buffer ? m_blocks_buffer + src_offset : nullptr;
        // Destino interleaved: frontbuffer + y*BITMAPBYTESPERROW + x_word
        u16* dst = reinterpret_cast<u16*>(
            (this->m_frontbuffer + dst_offset).ptr());

        // Un único blit de BLOCKPLANELINES líneas y words_per_block words.
        // El Blitter ve el bitmap interleaved como una sola columna tall.
        // bltsize = BLOCKPLANELINES*64 + words
        const u16 words = words_per_block;
        // Módulos del Blitter (ver §6)
        const s16 src_mod = eng::graphics::mod16(static_cast<s32>(src_bytes_per_row) - static_cast<s32>(words) * 2);
        const s16 dst_mod = eng::graphics::mod16(static_cast<s32>(this->m_bytes_per_row) - static_cast<s32>(words) * 2);

        // Para que FramePlan::add_tile_block_copy valide, los strides deben
        // ser no nulos. En modo interleaved el stride real es el módulo, pero
        // el validador exige plane_stride_bytes !=0. Usamos 2 como placeholder
        // inocuo porque bitplane_count=1 sólo itera una vez.
        return {
            graphics::BlitJobKind::TileBlockCopy,
            graphics::BlitPtr {},
            graphics::BlitPtr::from_storage(reinterpret_cast<const u16*>(src)),
            graphics::BlitPtr::from_storage(dst),
            words,
            this->m_block_planes_lines,
            src_mod,
            dst_mod,
            1, // ¡un solo blit para todos los planos interleaved!
            0,
            2, // source_plane_stride_bytes (placeholder, ver arriba)
            2, // destination_plane_stride_bytes
            false
        };
    }

    // -------------------------------------------------------------------------
    // Scroll corkscrew (XYLimited) — port fiel de Scroller_XYLimited/main.c
    // -------------------------------------------------------------------------
    // Estado derivado del corkscrew:
    //   mapblockx = mapposx / TW, stepx = mapposx & (TW-1)
    //   mapblocky = mapposy / TH, stepy = mapposy & (TH-1)
    //   block_videoposy = (mapposy / TH * TH) % bitmap_height
    //                    (fila de bloque físico donde se pinta la banda de
    //                     staging; se deriva, no se mantiene incremental)
    //   TWOBLOCKSTEP = bitmap_blocks_per_row - tile_height
    // La fila/columna entrante se dibuja en la banda de staging
    // `block_videoposy` (2 bloques por encima del display visible, que el
    // display alcanza al dar la vuelta en `display_height`), y las posiciones
    // X/Y usan `% display_height` / `% display_planelines` para quedarse
    // dentro del bucle vertical del display (mismo invariante que el original).

    constexpr u16 block_videoposy() const {
        // Banda de staging: SIEMPRE dentro del bucle de display (0..display_height),
        // nunca en las filas extra (display_height..bitmap_height) que usa el
        // planeaddx walk horizontal. Envolver en bitmap_height hacía que cada
        // map_width px la fila entrante se dibujara en las filas extra que el
        // display SÍ muestra al scrollear en X (tile visible en el área de
        // pantalla y banda de staging sin refrescar).
        return static_cast<u16>(this->dmod1(
            (static_cast<u32>(m_scroll.state().mapposy) / this->cth()) * this->cth()));
    }
    /// Añade el blit de un bloque y, en modo lineal (espejo), también el espejo.
    /// Devuelve false si el plan no admite el/los job(s).
    /// Overlays animados de la celda de mapa (mapx,mapy) en el destino físico
    /// (x, y_planeline). Se pinta despuǸs del bloque base y comparte su destino.
    void set_animator(const TileAnimator* animator) { this->m_cfg.animator = animator; }

    bool add_draw(graphics::FramePlan& plan, u16 x, u16 y, u16 mapx, u16 mapy) {
        // Política de borde en mapa NO envolvente (wrap_x/y = 0): el corkscrew
        // pre-pinta con bitmaps de margen "bitmap_blocks_per_row" (22) por delante
        // de la cámara. Al acercarse al borde derecho del mapa (40 columnas),
        // `mapx = mapblockx + bpr` supera this->map_width_blocks y `tile_at` devolvería
        // `edge_tile` (=0 en la demo 201, índice de tile REAL del banco, no
        // empty_tile 0xFFFF): el Blitter pintaría contenido equivocado que la
        // ventana revela junto al borde (tiles "top/right" rotos). Como en un mapa
        // edge-clamped no hay tiles al otro lado, se CLAMPA el origen a la última
        // columna/fila del mapa: repetición del borde con índice válido (misma
        // semántica que wrap=0 en tile_at, pero con una celda existente).
        if (this->m_cfg.map.wrap_x == 0 && mapx >= this->map_width_blocks()) {
            mapx = static_cast<u16>(this->map_width_blocks() - 1);
        }
        if (this->m_cfg.map.wrap_y == 0 && mapy >= this->map_height_blocks()) {
            mapy = static_cast<u16>(this->map_height_blocks() - 1);
        }
        // Tile 'vacío' (empty_tile): NO se pinta (no consume slot de Blitter).
        if (this->m_cfg.map.is_empty(this->map_tile_at(mapx, mapy))) return true;
        if (!plan.add_tile_block_copy(draw_block_job(x, y, mapx, mapy))) return false;
        if (this->m_linear_display) {
            // Espejo: mismo bloque en planelínea y + this->m_mirror_planelines (copia del bucle).
            const u16 ym = static_cast<u16>(static_cast<u32>(y) + this->m_mirror_planelines);
            if (!plan.add_tile_block_copy(draw_block_job(x, ym, mapx, mapy))) return false;
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // Primitiva de dibujo de framebuffer (sprites/blobs/CPU): abstrae el layout
    // de memoria real. TODAS las rutinas de dibujo futuras deben pasar por aquí.
    //   - split (canónico): el destino se envuelve en el bucle y, si el rect
    //     cruza `display_height`, se parte en dos (regla "split = parte").
    //   - linear_display (espejo): el destino se dibuja en el bucle y se duplica
    //     al espejo (regla "espejo = duplica").
    // Toma COORDENADAS DE MUNDO (wx, wy en píxeles): el display ya resuelve el
    // desplazamiento de cámara (planeaddx/display_offset), así que un objeto en
    // el mundo se dibuja aquí y aparece en pantalla scrolleando con el fondo.
    // Para un objeto fijo en pantalla (HUD), el caller convierte pantalla→mundo
    // con `mapposx()+x` / `mapposy()+y` cada frame.
    // -------------------------------------------------------------------------

    /// Emite un rectángulo planar (separate planes) de `seg` filas de pantalla en
    /// la planelínea `planeline_start` (por plano p: planeline_start+p).
    bool emit_world_rect(graphics::FramePlan& plan, const u16* src, u16 x_byte,
                          u32 planeline_start, u16 words, u16 seg_rows,
                          u16 src_row_bytes, u32 src_plane_stride, u8 planes) {
        const s16 src_mod = eng::graphics::mod16(static_cast<s32>(src_row_bytes) - static_cast<s32>(words) * 2);
        const s16 dst_mod = eng::graphics::mod16(static_cast<s32>(this->m_bytes_per_row) * static_cast<s32>(planes) - static_cast<s32>(words) * 2);
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = src + static_cast<u32>(p) * (src_plane_stride / 2u);
            u16* d = reinterpret_cast<u16*>((this->m_frontbuffer +
                (planeline_start + static_cast<u32>(p)) * this->m_bytes_per_row + x_byte).ptr());
            graphics::BlitJob job {
                graphics::BlitJobKind::CopyRect, graphics::BlitPtr {}, graphics::BlitPtr::from_storage(s), graphics::BlitPtr::from_storage(d),
                words, seg_rows, src_mod, dst_mod,
                1, 0, src_plane_stride, static_cast<u32>(this->m_bytes_per_row * planes), false
            };
            if (!plan.add_copy_rect(job)) return false;
        }
        return true;
    }

    /// Dibuja un rectángulo planar (separate planes, `planes` planos, cada fila de
    /// `src_row_bytes`, cada plano separado `src_plane_stride` bytes) en el MUNDO.
    /// `wx` debe ser múltiplo de 16 (word-aligned). El origen `src` debe estar en
    /// Chip RAM (el Blitter no lee .rodata). Gestiona la costura y el espejo.
    /// La fuente viaja como `Span`: se valida que cubra `src_plane_stride*planes`.
    bool add_world_bitmap(graphics::FramePlan& plan, // override de Playfield
                          Span<const u16> src, s32 wx, s32 wy, u16 w, u16 h,
                          u16 src_row_bytes, u32 src_plane_stride, u8 planes,
                          u8 source_shift = 0u, bool descending = false,
                          RasterOp op = RasterOp::Copy) override {
        // El layout corkscrew no usa shift fino / descendente / op lógica en el
        // camino de world bitmap (los consumidores no los pasan); se ignoran.
        (void)source_shift;
        (void)descending;
        (void)op;
        if (!this->m_initialized || src.empty() || planes == 0) return false;
        if (wx < 0 || (wx & 15) != 0) return false;
        const s32 loop = this->dmod2(wy);
        const u16 words = static_cast<u16>(w / 16u);
        const u32 need_src = (planes > 1u ? (static_cast<u32>(planes - 1u) * (src_plane_stride / 2u)) : 0u)
                           + (h > 1u ? (static_cast<u32>(h - 1u) * (src_row_bytes / 2u)) : 0u)
                           + static_cast<u32>(words);
        if (src.size() < need_src) return false;
        const u16* sbase = src.data();
        const u16 x_byte = static_cast<u16>((wx / 8u) & 0xfffeu);
        s32 r = loop;
        u16 remaining = h;
        while (remaining > 0) {
            const u16 seg = static_cast<u16>(
                (r + remaining > this->m_display_height) ? (this->m_display_height - r) : remaining);
            // Dibujo en el bucle (partido por la costura si cruza display_height).
            if (!emit_world_rect(plan, sbase, x_byte, static_cast<u32>(r) * planes,
                    words, seg, src_row_bytes, src_plane_stride, planes)) return false;
            // En modo lineal, duplicar al espejo para que el framebuffer quede coherente.
            if (this->m_linear_display) {
                if (!emit_world_rect(plan, sbase, x_byte,
                        static_cast<u32>(r + this->m_display_height) * planes,
                        words, seg, src_row_bytes, src_plane_stride, planes)) return false;
            }
            remaining = static_cast<u16>(remaining - seg);
            r = 0; // la segunda parte envuelve al inicio del bucle
        }
        return true;
    }
    /// Emite un rectángulo planar ENMASCARADO (cookie-cut) de `seg` filas en la
    /// planelínea `planeline_start`. `mask` es un ÚNICO plano de 1 bit compartido
    /// por todos los bitplanes (regla `dest = (mask & src) | (~mask & dest)`,
    /// `BlitJobKind::MaskedBobCookieCut`): donde la máscara es 0 se conserva el
    /// fondo (transparencia), donde es 1 se escribe el plano. El plano de máscara
    /// tiene el MISMO layout de fila que un plano fuente (el backend reutiliza
    /// `source_modulo_bytes` para el canal A=masks, ver amiga.cpp).
    bool emit_world_rect_masked(graphics::FramePlan& plan, const u16* src, const u16* mask,
                                u16 x_byte, u32 planeline_start, u16 words, u16 seg_rows,
                                u16 src_row_bytes, u32 src_plane_stride, u8 planes,
                                u8 source_shift = 0u) {
        const s16 src_mod = eng::graphics::mod16(static_cast<s32>(src_row_bytes) - static_cast<s32>(words) * 2);
        // El destino es un bloque de `words` palabras, pero si `source_shift` > 0
        // el barrel del Blitter lee además la palabra de guarda de la fuente y
        // de la máscara (por eso `src_row_bytes` incluye `words + 1`).
        const s16 dst_mod = eng::graphics::mod16(static_cast<s32>(this->m_bytes_per_row) * static_cast<s32>(planes) - static_cast<s32>(words) * 2);
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = src + static_cast<u32>(p) * (src_plane_stride / 2u);
            u16* d = reinterpret_cast<u16*>((this->m_frontbuffer +
                (planeline_start + static_cast<u32>(p)) * this->m_bytes_per_row + x_byte).ptr());
            graphics::BlitJob job {
                graphics::BlitJobKind::MaskedBobCookieCut, graphics::BlitPtr::from_storage(mask), graphics::BlitPtr::from_storage(s), graphics::BlitPtr::from_storage(d),
                words, seg_rows, src_mod, dst_mod,
                1, source_shift, src_plane_stride, static_cast<u32>(this->m_bytes_per_row * planes), false
            };
            if (!plan.add_masked_bob(job)) return false;
        }
        return true;
    }

    /// **Blit de mundo con ventana** sobre una banda planar completa: `band` es una copia
    /// del fondo con layout `[plano][fila][band_words_per_row]` (p. ej. el carril guardado
    /// al inicio) y `first_word` selecciona la ventana de `w_words` palabras dentro de
    /// cada fila — el borrado barato de la posición anterior de un objeto que se mueve
    /// (restaurar 2 palabras en vez de toda la banda). La columna de la banda ES la
    /// coordenada de mundo: `wx` debe ser `first_word·16` (la banda empieza en x=0).
    /// La ventana y el span se validan (`eng::setup::check_world_window` es la versión
    /// de setup); el resto de la costura/espejo lo gestiona `add_world_bitmap`.
    /// \param plan destino de los trabajos. \param band banda completa (Chip, palabra a palabra).
    /// \param first_word columna inicial de la ventana (palabras). \param wx x de mundo de la
    ///        ventana (múltiplo de 16, `first_word·16`). \param wy línea del anillo del blit.
    /// \param w_words ancho de la ventana en palabras. \param h filas. \param
    ///        band_words_per_row palabras por fila de la banda. \param planes planos.
    /// \return `true` si la ventana es válida y los trabajos se encolaron.
    bool add_world_window(graphics::FramePlan& plan, Span<const u16> band, u16 first_word,
                          s32 wx, s32 wy, u16 w_words, u16 h, u16 band_words_per_row, u8 planes) {
        if (!this->m_initialized || band.empty() || planes == 0u || w_words == 0u ||
            band_words_per_row == 0u) {
            return false;
        }
        if (static_cast<u32>(first_word) + static_cast<u32>(w_words) >
            static_cast<u32>(band_words_per_row)) {
            return false;
        }
        // La ventana cae en su columna de mundo (la banda empieza en x=0).
        if (wx != static_cast<s32>(first_word) * 16) {
            return false;
        }
        const u32 row_bytes = static_cast<u32>(band_words_per_row) * 2u;
        const u32 plane_stride = row_bytes * static_cast<u32>(h);
        const u16* base = band.data() + first_word;
        const usize avail = band.size() - first_word;
        return add_world_bitmap(plan, Span<const u16> {base, avail}, wx, wy,
                                static_cast<u16>(w_words * 16u), h,
                                static_cast<u16>(row_bytes), plane_stride, planes);
    }

    /// Dibuja un BOB planar con máscara de transparencia en el MUNDO. Igual que
    /// `add_world_bitmap` (origen en Chip RAM, `wx` múltiplo de 16, costura y
    /// espejo gestionados) pero con un plano de máscara de 1 bit compartido con
    /// el layout de fila de un plano fuente: donde el bit es 0 se conserva el
    /// fondo, donde es 1 se escribe el BOB.
    /// **No es la vía de objetos**: para actores/BOBs de juego, usar
    /// `Band::bob_target()` + `scene::BobLayer`/`FastBobLayer` (o `BobEmitter`),
    /// que centralizan el `FramePlan`, el destino y el reparto de recursos.
    bool add_world_bitmap_masked(graphics::FramePlan& plan, // override de Playfield
                                 Span<const u16> src, Span<const u16> mask, s32 wx, s32 wy,
                                 u16 w, u16 h, u16 src_row_bytes, u32 src_plane_stride,
                                 u8 planes, u8 source_shift = 0u) override {
        if (!this->m_initialized || src.empty() || mask.empty() || planes == 0) return false;
        if ((w & 15u) != 0u) return false;
        // `wx` es la columna de **mundo**. El ring guarda `world_x % bitmap_width`;
        // el display empieza en `planeaddx*8 - fine` (la cámara), así que un objeto
        // en `world_x` aparece en `world_x - camx` sin que el llamador reste la base
        // de la cámara. El ancho del ring (352) es múltiplo de 16, por lo que el
        // shift fino del mundo coincide con el del anillo.
        s32 rx = wx % static_cast<s32>(this->m_bitmap_width);
        if (rx < 0) rx += static_cast<s32>(this->m_bitmap_width);
        const u8 shift = (source_shift != 0u) ? source_shift : static_cast<u8>(rx & 15);
        const u16 words = static_cast<u16>(w / 16u);
        if (shift != 0u && static_cast<u32>(src_row_bytes) < (static_cast<u32>(words) + 1u) * 2u) return false;
        const s32 loop = this->dmod2(wy);
        // Contrato de tamaño: el origen cubre los planes; la máscara, una viaje.
        const u32 need_src = (planes > 1u ? (static_cast<u32>(planes - 1u) * (src_plane_stride / 2u)) : 0u)
                           + (h > 1u ? (static_cast<u32>(h - 1u) * (src_row_bytes / 2u)) : 0u)
                           + static_cast<u32>(words) + (shift != 0u ? 1u : 0u);
        const u32 need_mask = (h > 1u ? (static_cast<u32>(h - 1u) * (src_row_bytes / 2u)) : 0u)
                            + static_cast<u32>(words) + (shift != 0u ? 1u : 0u);
        if (src.size() < need_src || mask.size() < need_mask) return false;
        const u16* sbase = src.data();
        const u16* mbase = mask.data();
        const u16 x_byte = static_cast<u16>((static_cast<u32>(rx) / 8u) & 0xfffeu);
        s32 r = loop;
        u16 remaining = h;
        while (remaining > 0) {
            const u16 seg = static_cast<u16>(
                (r + remaining > this->m_display_height) ? (this->m_display_height - r) : remaining);
            if (!emit_world_rect_masked(plan, sbase, mbase, x_byte, static_cast<u32>(r) * planes,
                    words, seg, src_row_bytes, src_plane_stride, planes, shift)) return false;
            if (this->m_linear_display) {
                if (!emit_world_rect_masked(plan, sbase, mbase, x_byte,
                        static_cast<u32>(r + this->m_display_height) * planes,
                        words, seg, src_row_bytes, src_plane_stride, planes, shift)) return false;
            }
            remaining = static_cast<u16>(remaining - seg);
            r = 0;
        }
        return true;
    }

/// Guarda la word que el blit plane-shifted va a pisar (guarda de 1 word).
    /// Es un SEAM del layout: el algoritmo (ScrollEngine) decide CUÁNDO, el
    /// playfield (sink) la ejecuta sobre su propio framebuffer.
    void save_word(u32 byte_offset) {
        m_savewordpointer = reinterpret_cast<u16*>(
            (this->m_frontbuffer + byte_offset).ptr());
        m_saveword = *m_savewordpointer;
    }
    void restore_saveword() {
        if (m_savewordpointer) *m_savewordpointer = m_saveword;
    }

    // --- ScrollSink: el algoritmo vive en `ScrollEngine` (scroll_engine.hpp);
    // estos cuatro métodos delegan en él; el playfield solo aporta el layout.

    /// Scroll de 1 px a la derecha (plane-shifted) — ScrollRight corkscrew.
    bool scroll_right(graphics::FramePlan& plan) {
        if (!this->m_initialized) return false;
        return m_scroll.scroll_right(plan, *this);
    }
    /// Avance a la derecha en **ráfaga** (`px` píxeles, geometría del cruce una vez por
    /// tramo): equivalente a `px` pasos de `scroll_right` (HOST-034). Lo usa el pre-scroll.
    bool scroll_burst_right_px(graphics::FramePlan& plan, eng::u16 px) {
        if (!this->m_initialized) return false;
        return m_scroll.burst_right_px(plan, *this, px);
    }
    /// Scroll de 1 px a la izquierda (no plane-shifted) — ScrollLeft corkscrew.
    bool scroll_left(graphics::FramePlan& plan) {
        if (!this->m_initialized) return false;
        return m_scroll.scroll_left(plan, *this);
    }
    /// Scroll vertical 1 px hacia abajo — ScrollDown corkscrew.
    bool scroll_down(graphics::FramePlan& plan) {
        if (!this->m_initialized) return false;
        return m_scroll.scroll_down(plan, *this);
    }
    /// Scroll vertical 1 px hacia arriba — ScrollUp corkscrew.
    bool scroll_up(graphics::FramePlan& plan) {
        if (!this->m_initialized) return false;
        return m_scroll.scroll_up(plan, *this);
    }

    /// Vista de hardware para el compositor (planeaddx + BPLCON1 + offset Y).
    PlayfieldHardwareView hardware_view() const override {
        PlayfieldHardwareView v {};
        v.bitplanes = this->m_frontbuffer;
        v.real_base = m_real_base;
        v.bitmap_bytes_per_row = this->m_bytes_per_row;
        v.planes = this->cplanes();
        v.bitmap_height = this->m_bitmap_height;
        v.viewport_w = this->m_cfg.viewport_w;
        v.viewport_h = this->m_cfg.viewport_h;
        v.videoposx = m_scroll.state().videoposx;
        v.mapposx = m_scroll.state().mapposx;
        v.videoposy = m_scroll.state().videoposy;
        v.mapposy = m_scroll.state().mapposy;

        // Mapeo cámara→registros por el mapper neutral (HOST-063): réplica exacta
        // de UpdateCopperlist (xlimited.c). El módulo va como NTTP
        // (SC.display_height) para resolverse en compile-time (fast_div, sin
        // __umodsi3); si SC no lo trae, el mapper cae al display_h runtime.
        const u16 I = this->fetch_scroll_pixels(this->m_cfg.fetch_mode);
        const RingDisplayMapping ring = map_ring_scroll<SC.display_height>(
            m_scroll.state().videoposx, m_scroll.state().videoposy, I, this->cth(),
            this->cplanes(), this->m_bytes_per_row, this->m_display_height, this->m_cfg.viewport_h,
            this->scroll_y(), this->m_linear_display);
        v.planeaddx = ring.planeaddx;
        if (this->m_cfg.parallax_plane < this->cplanes()) {
            // Plano de fondo (RoboCod). Con soft DPF doble-buffer el display lee el
            // buffer delantero (`bg_plane_base`); `parallax_planeaddx` solo se usa en
            // el modo antiguo de puntero por plano (parallax_div != 0).
            v.parallax_plane = this->m_cfg.parallax_plane;
            v.bg_plane_base = m_soft_dpf.double_buffered()
                                  ? m_soft_dpf.display_base()
                                  : Address<MemoryKind::Chip> {};
            if (this->m_cfg.parallax_div != 0u) {
                const s32 ppos = (m_scroll.state().mapposx / this->m_cfg.parallax_div) +
                                 static_cast<s32>(I) - 1;
                v.parallax_planeaddx = static_cast<u32>(ppos / I) * (I / 8u);
            }
        }
        v.bplcon1 = ring.bplcon1;
        v.bpl1mod = this->m_bpl1mod;
        v.bpl2mod = this->m_bpl2mod;
        v.display_height = this->m_display_height;
        v.display_offset = ring.display_offset;
        v.planeaddy = ring.planeaddy;
        v.split_line = ring.split_line;
        v.split_active = ring.split_active;
        v.split_planeaddy = 0; // fila 0 (los punteros del split solo suman planeaddx)
        // plane_bytes para validación: bytes totales
        v.plane_bytes = static_cast<u32>(this->m_bytes_per_row * this->m_bitmap_height * this->cplanes());
        return v;
    }

    // Accesores para verificación y demo
    constexpr s32 mapposx() const override { return m_scroll.state().mapposx; }
    constexpr s32 videoposx() const override { return m_scroll.state().videoposx; }
    constexpr s32 mapposy() const override { return m_scroll.state().mapposy; }
    constexpr s32 videoposy() const override { return m_scroll.state().videoposy; }

    /// Columna de mundo de un objeto FIJO en la columna de pantalla `sx`.
    constexpr s32 screen_to_world_x(s16 sx) const {
        return m_scroll.state().mapposx + sx;
    }
    /// Fila de mundo de un objeto FIJO en la fila de pantalla `sy`. La ventana
    /// visible NO empieza en videoposy (la banda de staging queda un bloque por
    /// encima): equivale a `(mapposy + tile_height + sy) % display_height`.
    constexpr s32 screen_to_world_y(s16 sy) const {
        return screen_to_bitmap_row(sy);
    }

    /// Fila (en píxeles) del bucle vertical donde empieza la ventana visible.
    /// Coincide con `(videoposy + tile_height) % display_height`.
    constexpr s32 display_offset() const {
        return static_cast<s32>(this->dmod1(static_cast<u32>(m_scroll.state().videoposy) +
            this->cth()));
    }

    /// ¿El split del corkscrew es SIEMPRE esperable (raster <= 255)?
    ///
    /// El raster del split = DIWSTRT_y + (display_height - display_offset). El
    /// máximo (cuando el split es necesario) es `DIWSTRT_y + viewport_h - 1 =
    /// viewport_h + 40`. El WAIT del Copper compara solo 8 bits (máx 255), así
    /// que el split es 100% fiable si `viewport_h + 40 <= 255`, es decir
    /// `viewport_h <= 215` (208 = 13 filas de tile, 192 = 12 filas). Con eso el
    /// modo split es CANÓNICO: 1 blit por operación, sin espejo ni artefacto.
    /// Para viewports más altos (p. ej. 256) el split puede caer en 256..296 y
    /// hace falta `linear_display` (espejo, 2× blits) para evitarlo.
    constexpr bool split_always_waitable() const {
        return static_cast<u16>(this->m_cfg.viewport_h + 40u) <= 255u;
    }

    /// Convierte una fila de pantalla (0 = arriba, sy < viewport_h) a la fila
    /// del bitmap (en píxeles) donde se dibuja. Las rutinas de dibujo de
    /// framebuffer (sprites/blobs/CPU) deben usar ESTA fila y, si cruzan la
    /// costura (en modo split), partir el rectángulo.
    ///
    ///   - linear_display: devuelve `display_offset + sy` sin envolver (la
    ///     lectura lineal entra en el espejo; el dibujo se hace en el bucle y
    ///     se duplica al espejo).
    ///   - split: devuelve `(display_offset + sy) % display_height` (envuelve en
    ///     el bucle; si el rect cruza `display_height` hay que partirlo).
    constexpr s32 screen_to_bitmap_row(s16 sy) const {
        const s32 row = display_offset() + sy;
        return this->m_linear_display ? row : this->dmod2(row);
    }

    /// Parchea las celdas visibles cuyos grupos de animación cambiaron de frame.
    /// Debe llamarse despuǸs de update (scroll) y antes de ejecutar el plan.
    /// \param plan destino de los trabajos.
    /// \param restore_background_layers máscara de capas (bit por índice) que
    ///        repintan el bloque base antes del frame enmascarado; evita restos
    ///        del frame anterior en celdas transparentes (por ejemplo, fogata).
    /// \return `false` si algún job no cabe o falla el dibujo del grupo.
    bool patch_animations(graphics::FramePlan& plan, u8 restore_background_layers = 0u) {
        return patch_animations_impl<false>(plan, restore_background_layers, nullptr);
    }

    /// Variante que recoge estadísticas; no se usa en la build normal.
    /// \param plan destino de los trabajos.
    /// \param restore_background_layers máscara de capas que repintan la base.
    /// \param stats salida con grupos/celdas visitados, pintados, diferidos y
    ///        descartados.
    /// \return `false` si algún job no cabe o falla el dibujo del grupo.
    bool patch_animations(graphics::FramePlan& plan, u8 restore_background_layers,
                          TilePatchStats* stats) {
        return patch_animations_impl<true>(plan, restore_background_layers, stats);
    }

private:
    /// Implementación única; `Collect` elimina en compilación los contadores
    /// cuando la demo no pide estadísticas.
    template <bool Collect>
    bool patch_animations_impl(graphics::FramePlan& plan, u8 restore_background_layers,
                               TilePatchStats* stats) {
        if constexpr (Collect) *stats = TilePatchStats {};
        else (void)stats;
        if (this->m_cfg.animator == nullptr) return true;
        const TileAnimator& anim = *this->m_cfg.animator;
        const s32 camx = m_scroll.state().mapposx;
        const s32 camy = m_scroll.state().mapposy;
        if (camx != m_last_anim_camx || camy != m_last_anim_camy) {
            m_last_anim_camx = camx;
            m_last_anim_camy = camy;
            // No se invalida todo el viewport: las celdas que siguen visibles
            // conservan su overlay; solo se dibujan las que aún no tienen bit.
            m_redraw_visible_anim = true;
        }
        if (!anim.any_changed() && !m_redraw_visible_anim) return true;
        const s32 mapw = static_cast<s32>(this->map_width_blocks()) * this->ctw();
        const s32 maph = static_cast<s32>(this->map_height_blocks()) * this->cth();
        if (mapw <= 0 || maph <= 0) return false;
        // Una división por frame (no 2 por celda): las comparaciones de visibilidad
        // se resuelven con un ajuste de rango porque wx/cam están normalizados.
        s32 camx_mod = camx % mapw;
        if (camx_mod < 0) camx_mod += mapw;
        s32 camy_mod = camy % maph;
        if (camy_mod < 0) camy_mod += maph;
        const s32 ring_base = this->display_offset();
        const s32 dh = static_cast<s32>(this->display_height());
        const u16 parity = static_cast<u16>(anim.tick() % 6u);
        // El plan de 207 admite grupos de hasta 3 celdas (3 * 6 planos = 18
        // jobs); un grupo de 4 celdas se difiere. Se dibuja lo que quepa y los
        // pendientes conservan `changed` para el siguiente turno de paridad.
        constexpr u16 kPatchJobLimit = 24u;
        constexpr u16 kJobsPerCell = 6u;
        bool pass_deferred = false;
        for (u16 g = 0u; g < anim.group_count(); ++g) {
            if constexpr (Collect) ++stats->groups_scanned;
            const bool changed = anim.group_changed(g);
            const bool parity_ok = ((g % 6u) == parity);
            if (!m_redraw_visible_anim && (!changed || !parity_ok)) continue;
            const TileAnimationGroupView& grp = anim.group(g);
            if (plan.blit_job_count() + static_cast<u16>(grp.cell_count) * kJobsPerCell >
                kPatchJobLimit) {
                pass_deferred = true;
                if constexpr (Collect) {
                    // Cuenta también los grupos posteriores que sí tocaban en
                    // este turno y quedan pendientes para el siguiente.
                    for (u16 pending = g; pending < anim.group_count(); ++pending) {
                        if (anim.group_changed(pending) && ((pending % 6u) == parity)) {
                            ++stats->groups_deferred;
                        }
                    }
                }
                break;
            }
            bool group_drawn = false;
            for (u16 ci = grp.first_cell; ci < static_cast<u16>(grp.first_cell + grp.cell_count); ++ci) {
                const TileAnimationCellView& cell = anim.cell(ci);
                if (!anim.test_cell_allowed(ci)) continue;
                const s32 wx = static_cast<s32>(cell.x) * this->ctw();
                const s32 wy = static_cast<s32>(cell.y) * this->cth();
                s32 sx = wx - camx_mod;
                if (sx < 0) sx += mapw;
                if (sx >= this->m_cfg.viewport_w) {
                    if constexpr (Collect) ++stats->cells_culled;
                    continue;
                }
                s32 sy = wy - camy_mod - static_cast<s32>(this->cth());
                if (sy < 0) sy += maph;
                if (sy >= this->m_cfg.viewport_h) {
                    if constexpr (Collect) ++stats->cells_culled;
                    continue;
                }
                if (m_redraw_visible_anim && !changed &&
                    ci < kTileAnimationMaxCells && m_anim_cell_visible[ci] != 0u) {
                    continue;
                }
                s32 ring_y = ring_base + sy;
                if (ring_y >= dh) ring_y -= dh;
                // Las celdas transparentes no deben conservar restos del frame
                // anterior: repinta el bloque base y despu������s el frame encima.
                if ((restore_background_layers & static_cast<u8>(1u << cell.layer)) != 0u) {
                    if (!this->add_draw(plan, static_cast<u16>(sx),
                            static_cast<u16>(ring_y), cell.x, cell.y)) {
                        return false;
                    }
                    if constexpr (Collect) {
                        ++stats->base_restores;
                        if (cell.layer < kTileAnimationMaxLayers) {
                            ++stats->base_restores_by_layer[cell.layer];
                        }
                    }
                }
                // Se usa el mismo dibujo de grupo que la pasada inicial: el
                // overlay vive en el anillo, no en el espejo lineal del display.
                if (!anim.draw_group(plan, m_anim_target, static_cast<u16>(dh),
                        static_cast<u16>(sx), static_cast<u16>(ring_y), g, false)) {
                    return false;
                }
                if (ci < kTileAnimationMaxCells) m_anim_cell_visible[ci] = 1u;
                group_drawn = true;
                if constexpr (Collect) {
                    ++stats->cells_patched;
                    if (cell.layer < kTileAnimationMaxLayers) {
                        ++stats->cells_patched_by_layer[cell.layer];
                    }
                }
            }
            if constexpr (Collect) {
                if (group_drawn) ++stats->groups_patched;
            }
            if (changed && (parity_ok || m_redraw_visible_anim)) anim.clear_group(g);
        }
        if (m_redraw_visible_anim && !pass_deferred) m_redraw_visible_anim = false;
        return true;
    }

public:
    /// Compone todas las celdas visibles de las capas vivas de `stack` usando el
    /// frame actual. Ejecuta en lotes para no desbordar el `FramePlan`.
    template <class Backend>
    bool draw_animation_stack(Backend& backend, graphics::FramePlan& plan,
                              const TileLayerStack& stack,
                              s16 only_map_row = -1, s16 screen_y = -1) {
        if (this->m_cfg.animator == nullptr || !stack.configured()) return true;
        const TileAnimator& anim = *this->m_cfg.animator;
        const s32 camx = m_scroll.state().mapposx;
        const s32 camy = m_scroll.state().mapposy;
        const s32 mapw = static_cast<s32>(this->map_width_blocks()) * this->ctw();
        const s32 maph = static_cast<s32>(this->map_height_blocks()) * this->cth();
        if (mapw <= 0 || maph <= 0) return false;
        s32 camx_mod = camx % mapw;
        if (camx_mod < 0) camx_mod += mapw;
        s32 camy_mod = camy % maph;
        if (camy_mod < 0) camy_mod += maph;
        const s32 ring_base = this->display_offset();
        const s32 dh = static_cast<s32>(this->display_height());
        plan.clear();
        plan.set_blit_budget_limits({8192, 16384, 64, 120});
        for (u16 layer = 0u; layer < anim.layer_count(); ++layer) {
            if (!stack.is_live(static_cast<u8>(layer))) continue;
            for (u16 ci = 0u; ci < anim.cell_count(); ++ci) {
                const TileAnimationCellView& cell = anim.cell(ci);
                if (cell.layer != layer) continue;
                if (!anim.test_cell_allowed(ci)) continue;
                if (only_map_row >= 0 && static_cast<s16>(cell.y) != only_map_row) continue;
                const s32 wx = static_cast<s32>(cell.x) * this->ctw();
                const s32 wy = static_cast<s32>(cell.y) * this->cth();
                s32 sx = wx - camx_mod;
                if (sx < 0) sx += mapw;
                if (sx >= this->m_cfg.viewport_w) continue;
                s32 sy = (only_map_row >= 0) ? static_cast<s32>(screen_y)
                    : wy - camy_mod - static_cast<s32>(this->cth());
                if (only_map_row < 0) {
                    if (sy < 0) sy += maph;
                    if (sy < 0) sy += maph;
                    if (sy >= this->m_cfg.viewport_h) continue;
                }
                s32 ring_y = ring_base + sy;
                if (ring_y >= dh) ring_y -= dh;
                if (!anim.draw_layer_block(plan, m_anim_target,
                        static_cast<u16>(dh), static_cast<u16>(sx),
                        static_cast<u16>(ring_y), cell.x, cell.y,
                        static_cast<u16>(layer), false)) {
                    return false;
                }
                if (ci < kTileAnimationMaxCells) m_anim_cell_visible[ci] = 1u;
                if (plan.blit_job_count() >= 64u) {
                    if (!backend.execute_frame_plan(plan)) return false;
                    plan.clear();
                    plan.set_blit_budget_limits({8192, 16384, 64, 120});
                }
            }
        }
        if (plan.blit_job_count() > 0u) {
            if (!backend.execute_frame_plan(plan)) return false;
            plan.clear();
        }
        return true;
    }

    /// Pase `after_actors`: redibuja celdas de capas `Occluder` solo si su rect
    /// visible intersecta alguna zona sucia.
    template <class Backend>
    bool draw_animation_occluders(Backend& backend, graphics::FramePlan& plan,
                                  const TileLayerStack& stack,
                                  eng::Span<const graphics::DirtyRect> dirty) {
        if (this->m_cfg.animator == nullptr || !stack.has_occluders()) return true;
        const TileAnimator& anim = *this->m_cfg.animator;
        const s32 camx = m_scroll.state().mapposx;
        const s32 camy = m_scroll.state().mapposy;
        const s32 mapw = static_cast<s32>(this->map_width_blocks()) * this->ctw();
        const s32 maph = static_cast<s32>(this->map_height_blocks()) * this->cth();
        if (mapw <= 0 || maph <= 0) return false;
        s32 camx_mod = camx % mapw;
        if (camx_mod < 0) camx_mod += mapw;
        s32 camy_mod = camy % maph;
        if (camy_mod < 0) camy_mod += maph;
        const s32 ring_base = this->display_offset();
        const s32 dh = static_cast<s32>(this->display_height());
        plan.clear();
        plan.set_blit_budget_limits({8192, 16384, 64, 120});
        for (u16 ci = 0u; ci < anim.cell_count(); ++ci) {
                const TileAnimationCellView& cell = anim.cell(ci);
                if (!anim.test_cell_allowed(ci)) continue;
                const s32 wx = static_cast<s32>(cell.x) * this->ctw();
            const s32 wy = static_cast<s32>(cell.y) * this->cth();
            s32 sx = wx - camx_mod;
            if (sx < 0) sx += mapw;
            if (sx >= this->m_cfg.viewport_w) continue;
            s32 sy = wy - camy_mod - static_cast<s32>(this->cth());
            if (sy < 0) sy += maph;
            if (sy < 0) sy += maph;
            if (sy >= this->m_cfg.viewport_h) continue;
            s32 ring_y = ring_base + sy;
            if (ring_y >= dh) ring_y -= dh;
            if (!stack.draw_occluders(anim, plan, m_anim_target,
                    static_cast<u16>(dh), static_cast<u16>(sx),
                    static_cast<u16>(ring_y), cell.x, cell.y, dirty)) {
                return false;
            }
            if (plan.blit_job_count() >= 64u) {
                if (!backend.execute_frame_plan(plan)) return false;
                plan.clear();
                plan.set_blit_budget_limits({8192, 16384, 64, 120});
            }
        }
        if (plan.blit_job_count() > 0u && !backend.execute_frame_plan(plan)) return false;
        plan.clear();
        return true;
    }

    /// Fija la posición de la cámara (píxeles de mundo) y sincroniza los punteros
    /// de display. Necesario para arrancar a media altura/ancho (p. ej. un shooter
    /// vertical de 10000 px que empieza abajo y sube). Debe llamarse tras `begin`
    /// y antes del `fill_screen`/primer frame.
    void set_camera(s32 x, s32 y) {
        m_scroll.state().mapposx = x;
        m_scroll.state().videoposx = x;
        m_scroll.state().mapposy = y;
        m_scroll.state().videoposy = static_cast<s32>(this->dmod2(y));
        m_scroll.state().previous_xdirection = 0; // DIRECTION_IGNORE
    }

    /// Reinicia el scroll a 0 sin re-reservar Chip RAM (para demo infinita).
    void reset_scroll() {        m_scroll.state().mapposx = 0;
        m_scroll.state().videoposx = 0;
        m_scroll.state().mapposy = 0;
        m_scroll.state().videoposy = 0;
        m_scroll.state().previous_xdirection = 0;
        m_savewordpointer = nullptr;
        m_saveword = 0;
    }

private:


    gfx::Bitmap m_bitmap {};   // capa de memoria (posee el bloque Chip)
    Address<MemoryKind::Chip> m_real_base {};
    // Soft DPF (RoboCod): la composición (vista del plano de fondo + doble buffer)
    // vive en `soft_dpf.hpp`; el playfield solo la configura y la consulta.
    SoftDpfComposition m_soft_dpf {};
    const u8* m_blocks_buffer = nullptr;
    ScrollEngine<XLimitedPlayfield<SC, MapT, Profile>, SC> m_scroll {}; // cámara + algoritmo (§7)
    u16* m_savewordpointer = nullptr;             // guarda de 1 word plane-shift (sink)
    u16 m_saveword = 0;
    graphics::BobTarget m_anim_target {};         // anillo para overlays animados
    s32 m_last_anim_camx = 0x7fffffff;            // cámara del último parche
    s32 m_last_anim_camy = 0x7fffffff;
    bool m_redraw_visible_anim = true;            // pasada de entrada tras scroll
    mutable u8 m_anim_cell_visible[kTileAnimationMaxCells] {};
    u8 m_max_step = 1;                            // salto máx. px/frame por eje (config)
};

} // namespace eng::playfield
