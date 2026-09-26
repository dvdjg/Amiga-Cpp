#pragma once

/// \file xlimited_playfield.hpp
/// `XLimitedPlayfield`: corkscrew 8-way con bitmap interleaved y wrap vertical. Definido
/// aparte de `xlimited_base.hpp`; `xlimited.hpp` es la cabecera de familia.

#include <eng/field/xlimited_base.hpp>
#include <eng/field/xlimited_mapping.hpp>

namespace eng::field {

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

    // No copiable (posee memoria Chip)
    XLimitedPlayfield(const XLimitedPlayfield&) = delete;
    XLimitedPlayfield& operator=(const XLimitedPlayfield&) = delete;


    /// Scroll de N píxeles por eje (especialización del playfield). Devuelve false
    /// si un borde del mapa bloqueó el avance (dirección inversa sin recorrido).
    ///
    /// `max_step` es el SALTO máximo configurable por eje y frame. El avance se
    /// ejecuta como `|dx|`+`|dy|` sub-pasos ATÓMICOS de 1 px: cada uno pinta su
    /// columna/fila entrante ANTES de avanzar `videopos` (paint-then-advance) y
    /// el reveal lo hace el Copper tras ejecutar el plan → NUNCA se muestra un
    /// píxel sin pintar, para cualquier salto ≤ max_step (16 px = la columna
    /// completa cada frame; el coste de Blitter crece ∝ salto).
    bool update_scroll(graphics::FramePlan& plan, s32 dx, s32 dy) override {
        if (!this->m_initialized) return false;
        m_dbg_ink_visible = false; // DEBUG: reinicio el flag del frame (hipótesis offset)
        // Perfil con `prefill`: la cámara avanza por TILES completos. Garantiza
        // que el cambio de dirección caiga en frontera de tile (`direction_latched`)
        // y que la franja entre alineada (plane-shift 0 al terminar). El perfil
        // progresivo conserva el paso exacto de la config (1 px/sub-paso).
        if constexpr (Profile::prefill) {
            dx = eng::field::snap_to_tiles(dx, this->ctw());
            dy = eng::field::snap_to_tiles(dy, this->cth());
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
            } else {
                for (s32 i = 0; i < dx; ++i) { if (!m_scroll.scroll_right(plan, *this)) return false; }
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

    /// DEBUG: ¿el frame pintó algún bloque de relleno dentro de la zona visible?
    constexpr bool dbg_ink_visible() const { return m_dbg_ink_visible; }
    /// DEBUG: fila del bucle (0..display_height) del último ink visible.
    constexpr u8 dbg_ink_visible_row() const { return m_dbg_ink_visible_row; }


    /// Reserva el bitmap interleaved y prepara el estado inicial.
    ///
    /// Reserva `bitmap_bytes_per_row * bitmap_height * planes` bytes en Chip
    /// RAM con alineación 16 (como `AllocBitMap(..., BMF_INTERLEAVED|BMF_CLEAR)`).
    /// `frontbuffer` apunta a `base + bitmapoffset` para los modos de fetch
    /// ancho (16 bytes para BPL32, 48 para 4x). En modo normal offset=0.
    bool begin(MemorySystem& memory, const XlimitedConfigT<MapT>& cfg) {
        // Verifica en compile-time que este playfield cumple el contrato del
        // algoritmo (`ScrollEngine`); hace el scroll portátil y explícito.
        static_assert(eng::field::ScrollSink<XLimitedPlayfield<SC, MapT, Profile>>,
            "XLimitedPlayfield debe cumplir el sink del ScrollEngine (corkscrew/XYLimited).");
        this->m_cfg = cfg;
        m_max_step = this->m_cfg.max_step ? this->m_cfg.max_step : 1; // salto configurable (≥1)
        // Perfil estático: si impone paso (perfiles rápidos), su valor gana; la
        // selección se hace con un tipo (ver `scroll_profile.hpp`/`FAST_SCROLL.md`).
        if constexpr (Profile::fill_tiles != 0u) {
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
        this->m_frontbuffer = Address<MemoryKind::Chip>::from_storage(m_bitmap.bytes().data()); // vía cruda interna (núcleo)
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
        eng::field::fill_parallax_pattern(this->m_frontbuffer.ptr(), this->m_bytes_per_row, this->m_cfg.planes,
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
    graphics::BlitJob make_bg_plane_copy_job(eng::Pattern pattern, u16 pattern_row_bytes,
                                             u16 src_x_pixels, u16 src_y) const {
        return m_soft_dpf.make_copy_job(pattern, pattern_row_bytes, src_x_pixels, src_y);
    }

    /// Soft DPF: conmuta el buffer de fondo delantero/trasero. Llamar TRAS escribir
    /// el blit de fondo (que va al buffer trasero) y ANTES de `compose()`.
    void bg_flip() { m_soft_dpf.flip(); }
    constexpr bool bg_double_buffered() const { return m_soft_dpf.double_buffered(); }

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
                auto job = draw_block_job(x, y, mapx, mapy);
                if (!plan.add_tile_block_copy(job)) return false;
                if (this->m_linear_display) {
                    const u16 ym = static_cast<u16>(static_cast<u32>(y) + this->m_mirror_planelines);
                    auto mjob = draw_block_job(x, ym, mapx, mapy);
                    if (!plan.add_tile_block_copy(mjob)) return false;
                }
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
        const s16 src_mod = static_cast<s16>(src_bytes_per_row - words * 2);
        const s16 dst_mod = static_cast<s16>(this->m_bytes_per_row - words * 2);

        // Para que FramePlan::add_tile_block_copy valide, los strides deben
        // ser no nulos. En modo interleaved el stride real es el módulo, pero
        // el validador exige plane_stride_bytes !=0. Usamos 2 como placeholder
        // inocuo porque bitplane_count=1 sólo itera una vez.
        return {
            graphics::BlitJobKind::TileBlockCopy,
            graphics::BlitSource {},
            graphics::BlitSource { reinterpret_cast<const u16*>(src) },
            graphics::BlitDest { dst },
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
        // DEBUG (hipótesis viewport-offset): ¿este bloque de relleno cae en las
        // filas del bucle que el display está mostrando AHORA mismo? Si sí, el
        // indice del viewport respecto al framebuffer hace visibles los tiles.
        {
            const s32 vps = m_scroll.state().videoposy;
            const u32 d = static_cast<u32>(this->dmod1(static_cast<u32>(vps) + this->cth()));
            const u32 row = static_cast<u32>(y) / this->cplanes(); // fila real del bucle
            const u32 rel = this->dmod1(row + this->m_display_height - this->dmod1(d));
            // Horiz. visible en el framebuffer (ventana que lee el chip a partir de
            // ROUND2(videoposx)): ignorar la columna derecha (x ≈ x0+bitmap_width).
            const u32 x0v = static_cast<u32>(m_scroll.state().videoposx & ~(this->ctw() - 1)) % this->m_bitmap_width;
            const u32 xb = static_cast<u32>(x) % this->m_bitmap_width;
            const bool xvis = (xb < x0v + this->m_cfg.viewport_w) && (xb + this->ctw() > x0v) ||
                              (x0v + this->m_cfg.viewport_w > this->m_bitmap_width && xb < (x0v + this->m_cfg.viewport_w) % this->m_bitmap_width);
            if (rel < this->m_cfg.viewport_h && xvis) { m_dbg_ink_visible = true; m_dbg_ink_visible_row = static_cast<u8>(row); }
        }
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
        const s16 src_mod = static_cast<s16>(src_row_bytes - words * 2);
        const s16 dst_mod = static_cast<s16>(this->m_bytes_per_row * planes - words * 2);
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = src + static_cast<u32>(p) * (src_plane_stride / 2u);
            u16* d = reinterpret_cast<u16*>((this->m_frontbuffer +
                (planeline_start + static_cast<u32>(p)) * this->m_bytes_per_row + x_byte).ptr());
            graphics::BlitJob job {
                graphics::BlitJobKind::CopyRect, graphics::BlitSource {}, graphics::BlitSource {s}, graphics::BlitDest {d},
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
                                u16 src_row_bytes, u32 src_plane_stride, u8 planes) {
        const s16 src_mod = static_cast<s16>(src_row_bytes - words * 2);
        const s16 dst_mod = static_cast<s16>(this->m_bytes_per_row * planes - words * 2);
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = src + static_cast<u32>(p) * (src_plane_stride / 2u);
            u16* d = reinterpret_cast<u16*>((this->m_frontbuffer +
                (planeline_start + static_cast<u32>(p)) * this->m_bytes_per_row + x_byte).ptr());
            graphics::BlitJob job {
                graphics::BlitJobKind::MaskedBobCookieCut, graphics::BlitSource {mask}, graphics::BlitSource {s}, graphics::BlitDest {d},
                words, seg_rows, src_mod, dst_mod,
                1, 0, src_plane_stride, static_cast<u32>(this->m_bytes_per_row * planes), false
            };
            if (!plan.add_masked_bob(job)) return false;
        }
        return true;
    }

    /// Dibuja un BOB planar con máscara de transparencia en el MUNDO. Igual que
    /// `add_world_bitmap` (origen en Chip RAM, `wx` múltiplo de 16, costura y
    /// espejo gestionados) pero con un plano de máscara de 1 bit compartido con
    /// el layout de fila de un plano fuente: donde el bit es 0 se conserva el
    /// fondo, donde es 1 se escribe el BOB.
    bool add_world_bitmap_masked(graphics::FramePlan& plan, // override de Playfield
                                 Span<const u16> src, Span<const u16> mask, s32 wx, s32 wy,
                                 u16 w, u16 h, u16 src_row_bytes, u32 src_plane_stride,
                                 u8 planes, u8 source_shift = 0u) override {
        (void)source_shift; // el corkscrew no usa shift fino en world bitmap
        if (!this->m_initialized || src.empty() || mask.empty() || planes == 0) return false;
        if (wx < 0 || (wx & 15) != 0) return false;
        const s32 loop = this->dmod2(wy);
        const u16 words = static_cast<u16>(w / 16u);
        // Contrato de tamaño: el origen cubre los planes; la máscara, una viaje.
        const u32 need_src = (planes > 1u ? (static_cast<u32>(planes - 1u) * (src_plane_stride / 2u)) : 0u)
                           + (h > 1u ? (static_cast<u32>(h - 1u) * (src_row_bytes / 2u)) : 0u)
                           + static_cast<u32>(words);
        const u32 need_mask = (h > 1u ? (static_cast<u32>(h - 1u) * (src_row_bytes / 2u)) : 0u)
                            + static_cast<u32>(words);
        if (src.size() < need_src || mask.size() < need_mask) return false;
        const u16* sbase = src.data();
        const u16* mbase = mask.data();
        const u16 x_byte = static_cast<u16>((wx / 8u) & 0xfffeu);
        s32 r = loop;
        u16 remaining = h;
        while (remaining > 0) {
            const u16 seg = static_cast<u16>(
                (r + remaining > this->m_display_height) ? (this->m_display_height - r) : remaining);
            if (!emit_world_rect_masked(plan, sbase, mbase, x_byte, static_cast<u32>(r) * planes,
                    words, seg, src_row_bytes, src_plane_stride, planes)) return false;
            if (this->m_linear_display) {
                if (!emit_world_rect_masked(plan, sbase, mbase, x_byte,
                        static_cast<u32>(r + this->m_display_height) * planes,
                        words, seg, src_row_bytes, src_plane_stride, planes)) return false;
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
    u8 m_max_step = 1;                            // salto máx. px/frame por eje (config)
    bool m_dbg_ink_visible = false;               // DEBUG: ink dentro de la zona visible
    u8 m_dbg_ink_visible_row = 0;                 // DEBUG: fila del bucle donde cayó el ink
};

} // namespace eng::field
