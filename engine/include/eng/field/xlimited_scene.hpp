#pragma once

/// \file xlimited_scene.hpp
/// Abstracción reutilizable sobre `XlimitedField` + compositores (single/dual).
///
/// RESPONSABILIDAD: es una ESCENA de composición para la familia x-limited
/// (corkscrew/XYLimited): posee UNO O DOS playfields de scroll (`XLimitedPlayfield`),
/// un lienzo HUD opcional (`CanvasPlayfield`), sprites y los compositores que
/// emiten el Copper. NO es el algoritmo: ese vive en `ScrollEngine`/playfield.
/// Ver `docs/architecture/XYLIMITED_ALGORITMO_GENERICO.md` (crítica: el HUD y el
/// par fg/bg son conceptos de composición; el algoritmo debería poder aplicarse
/// a cualquier `Playfield`, incluido el HUD si se quisiera un panel con scroll).
///
/// Esta capa es la que un programa (o una librería de programación) usa para
/// montar una escena corkscrew sin re-implementar el orquestado: configuración
/// declarativa, banco de bloques generado, relleno inicial, pre-scroll, camino
/// de direcciones para validación y composición single o DPF. El `main.cpp` de
/// la demo queda como un consumidor fino de esta escena.
///
/// Incluye además:
///   - `xlimited_build_blocks_bitmap`: construye el BlocksBitmap de Steger
///     (banco interleaved 320px) a partir de un generador de words por fila,
///     independiente del juego.
///   - `XlimitedSceneConfig`: configuración declarativa (geometría, mapa,
///     dual/parallax y camino de scroll).
///   - `XlimitedScene`: orquesta `update`/`fill`/`pre_scroll`/`compose`/`install`
///     para uno o dos playfields.
///
/// Reglas del engine: sin heap dinámico, sin RTTI, gnu++23. Los mapas y paletas
/// los aporta la aplicación (arrays estáticos o `MemoryBlock`).

#include <eng/core/sinetable.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/surface.hpp>
#include <eng/field/xlimited.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {


/// Generador de la word de una fila de un tile simbólico (glyph, variant, row,
/// plane). El caller incrusta su paleta/base/transparencia en el callback.
using BlocksRowFn = eng::u16 (*)(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane);

/// Construye el banco de bloques interleaved (BlocksBitmap de Steger, 320 px de
/// ancho, 40 bytes por planelínea) con `tile_count` tiles de
/// `tile_width`×`tile_height` dispuestos en `(tile % (320/tile_width), tile/...)`.
/// Devuelve un `MemoryBlock` en Chip RAM (inválido si no hay memoria).
inline MemoryBlock xlimited_build_blocks_bitmap(
    MemorySystem& memory,
    eng::u8 planes,
    eng::u16 tile_width,
    eng::u16 tile_height,
    eng::u16 tile_count,
    BlocksRowFn row_fn) {
    const eng::u32 src_bytes_per_row = 320u / 8u; // 40
    const eng::u32 blocks_per_row = 320u / tile_width;
    // El banco crece con `tile_count`, NO con el 256 fijo del original de Steger
    // (que solo reservaba un area de 320x256 = ~320 tiles a 16px). Con maps reales
    // de cientos de tiles hay que reservar las planelineas necesarias:
    //    filas_bloque = ceil(tile_count / blocks_per_row)
    //    planelineas   = filas_bloque * (tile_height*planes)
    // `draw_block_job` direcciona `(block/20, block%20)` sin wrap en Y, así que no
    // hay que tocar el blit: solo el tamaño del buffer fisico.
    const eng::u32 block_rows = (tile_count + blocks_per_row - 1u) / blocks_per_row;
    const eng::u32 height =
        block_rows * (static_cast<eng::u32>(tile_height) * planes); // planelineas totales
    const eng::u32 bytes = src_bytes_per_row * height;
    MemoryBlock block = memory.chip.allocate(bytes, 16);
    if (!block.valid() || row_fn == nullptr) return block;
    eng::u8* data = static_cast<eng::u8*>(block.data);
    for (eng::u32 i = 0; i < bytes; ++i) data[i] = 0;
    for (eng::u16 tile = 0; tile < tile_count; ++tile) {
        const eng::u8 glyph = static_cast<eng::u8>(tile & 15u);
        const eng::u8 variant = static_cast<eng::u8>((tile >> 4u) & 3u);
        const eng::u16 bx = tile % blocks_per_row;
        const eng::u16 by = tile / blocks_per_row;
        const eng::u32 base_pl = static_cast<eng::u32>(by) *
                                 (static_cast<eng::u32>(tile_height) * planes) * src_bytes_per_row;
        for (eng::u16 row = 0; row < tile_height; ++row) {
            for (eng::u8 plane = 0; plane < planes; ++plane) {
                const eng::u16 word = row_fn(glyph, variant, static_cast<eng::u8>(row), plane);
                for (eng::u16 w = 0; w < tile_width / 16u; ++w) {
                    const eng::u16 out_word = (w == 0) ? word
                        : static_cast<eng::u16>(word ^ 0x0ff0u);
                    const eng::u32 planeline = static_cast<eng::u32>(row) * planes + plane;
                    const eng::u32 off = (base_pl + planeline * src_bytes_per_row) +
                        bx * (tile_width / 8u) + w * 2u;
                    data[off] = static_cast<eng::u8>(out_word >> 8);
                    data[off + 1] = static_cast<eng::u8>(out_word & 0xff);
                }
            }
        }
    }
    return block;
}

/// Banco a partir de un tilebank INDEXADO REAL (pipeline EHB de 201).
///
/// Diferencia clave con `xlimited_build_blocks_bitmap`: la `row_fn` del método
/// generativo solo admite 64 glyph × 4 variant = 256 tiles, insuficiente para
/// maps reales de cientos de tiles (201 tiene 1149). Aquí el banco se construye
/// desde el tilebank crudo `tile_{t}[r*16+c]` (1 byte por píxel, stride fijo
/// `stride`, índice EHB 0..63 en convención BASES-PRIMERO) leyendo por índice de
/// tile completo, sin descomponer en glyph/variant.
///
/// Conversión EHB -> planos interleaved (mismo mapeo que fill_planes de 201):
/// el índice `v` ya es el número de color EHB absoluto, así que el bit p (0..5)
/// del índice es el bit del plano p (bit 5 = half). No hay ningún base-offset
/// extra que incrustar (a diferencia de pf_plane_row de 107).
inline MemoryBlock xlimited_build_blocks_bitmap_from_indexed(
    MemorySystem& memory,
    eng::u8 planes,
    eng::u16 tile_width,
    eng::u16 tile_height,
    eng::u16 tile_count,
    const eng::u8* indexed,
    eng::u32 stride) {
    const eng::u32 src_bytes_per_row = 320u / 8u; // 40 (BLOCKSWIDTH/8)
    const eng::u32 blocks_per_row = 320u / tile_width;
    const eng::u32 block_rows = (tile_count + blocks_per_row - 1u) / blocks_per_row;
    const eng::u32 height =
        block_rows * (static_cast<eng::u32>(tile_height) * planes); // planelineas totales
    const eng::u32 bytes = src_bytes_per_row * height;
    MemoryBlock block = memory.chip.allocate(bytes, 16);
    if (!block.valid() || indexed == nullptr) return block;
    eng::u8* data = static_cast<eng::u8*>(block.data);
    for (eng::u32 i = 0; i < bytes; ++i) data[i] = 0;
    const eng::u32 tw8 = tile_width / 8u; // bytes por planelínea de tile (2 a 16px)
    for (eng::u16 tile = 0; tile < tile_count; ++tile) {
        const eng::u16 bx = tile % blocks_per_row;
        const eng::u16 by = tile / blocks_per_row;
        const eng::u32 base_pl = static_cast<eng::u32>(by) *
                                 (static_cast<eng::u32>(tile_height) * planes) * src_bytes_per_row;
        const eng::u8* src = indexed + static_cast<eng::u32>(tile) * stride;
        for (eng::u16 row = 0; row < tile_height; ++row) {
            // 16 píxeles = 1 word por plano (bit 15..0, MSB primero como en Amiga).
            for (eng::u8 plane = 0; plane < planes; ++plane) {
                const eng::u16 bit = static_cast<eng::u16>(1u << plane);
                eng::u16 word = 0;
                for (eng::u16 c = 0; c < tile_width && c < 16u; ++c) {
                    const eng::u8 v = src[static_cast<eng::u32>(row) * tile_width + c];
                    if ((v & bit) != 0) word = static_cast<eng::u16>(word | (0x8000u >> c));
                }
                const eng::u32 planeline = static_cast<eng::u32>(row) * planes + plane;
                const eng::u32 off = (base_pl + planeline * src_bytes_per_row) + bx * tw8;
                data[off] = static_cast<eng::u8>(word >> 8);
                data[off + 1] = static_cast<eng::u8>(word & 0xff);
            }
        }
    }
    return block;
}

/// Configuración declarativa de una escena corkscrew.
///
/// Overlay compuesto encima del campo de scroll (p. ej. un HUD o un panel).
/// Es una capa INDEPENDIENTE (`CanvasPlayfield`) recortada por el Copper en una
/// zona; NO forma parte del algoritmo de scroll. Por eso vive en la config de la
/// ESCENA (composición), no en la del playfield.
struct XlimitedOverlayConfig {
    eng::u16 height = 0;              // 0 = sin overlay. Resta filas al área VISIBLE
    eng::u8 planes = 4;               //   del campo, pero NO al anillo del scroll.
    const eng::u16* palette = nullptr; // paleta del overlay (0..2^planes-1)
};

/// Composición de DOS playfields (dual playfield / DPF) o de un scroll + un
/// lienzo plano (`fg_canvas`). Son ROLES de composición (profundidad/parallax),
/// no del algoritmo: el scroll XYLimited se aplica a cada playfield por separado.
struct XlimitedDualConfig {
    bool enabled = false;             // DPF: dos playfields (3+3 o con lienzo)
    bool fg_canvas = false;           // DPF heterogéneo: el "fg" es un CanvasPlayfield
    bool parallax_x = false;          // el segundo playfield a velocidad reducida en X
    eng::u8 parallax_x_div = 2;
    eng::u8 parallax_y_div = 1;       // 1 = comparte el split vertical
};

/// Conductor de VALIDACIÓN (recorrido de las 8 direcciones del harness). Es un
/// andamiaje de demo/test, no un servicio del scroll; vive separado para que la
/// escena reutilizable no dependa de un "camino" concreto.
struct XlimitedPathConfig {
    eng::u8 effect = 0;               // 0=ciclo, 1..8=dirección única
    eng::u8 start_phase = 0;          // fase inicial del ciclo (0..7)
    eng::u32 phase_frames = 1000;     // frames por fase
    eng::s32 pre_scroll = 1024;       // px de pre-scroll para fases reversas/diagonales
};

/// Un solo campo (single) o dos (DPF). Para DPF, `planes` es la profundidad POR
/// playfield (3) y `dual.enabled=true`. Las tres sub-configs separan conceptos:
/// `hud` (overlay de composición), `dpf` (composición de capas), `path`
/// (validación). El scroll (geometría+algoritmo) es lo que queda plano.
struct XlimitedSceneConfig {
    // --- Geometría del campo de scroll (intrínseca del playfield) -----------
    eng::u16 viewport_w = 320;
    eng::u16 viewport_h = 256;       // alto visible del playfield principal
    eng::u16 tile_width = 16;
    eng::u16 tile_height = 16;
    eng::u8 planes = 4;              // profundidad por playfield (4 single, 3 DPF)
    eng::u8 fetch_mode = 0;          // 0=normal, 1/2=BPL32, 3=BPL32+BPAGEM
    eng::u16 bitmap_width = 0;       // 0 = auto: viewport + EXTRAWIDTH
    eng::u16 visible_tile_bias_x = 0; // 1 = offset visible (0,0) empieza en map[0][0]
    eng::u16 visible_tile_bias_y = 0; // 1 = la guarda superior usa la última fila

    // --- Overlay (HUD/panel) compuesto encima ------------------------------
    XlimitedOverlayConfig hud {};

    // --- Mundo ---------------------------------------------------------------
    TileLayerMap map {};             // mapa de PF1 (cells/wrap/edge)
    TileLayerMap map2 {};            // mapa de PF2 (si dual y no vacío; si no, reusa map)
    eng::u16 tileset_count = 64;
    BlocksRowFn fg_row_fn = nullptr; // generador de filas de PF1 (incrusta base/transparencia)
    BlocksRowFn bg_row_fn = nullptr; // generador de filas de PF2 (DPF)
    // Tilebank INDEXADO REAL (pipeline EHB): si se suministra, el banco se
    // construye con `xlimited_build_blocks_bitmap_from_indexed` leyendo tile por
    // tile completo (soporta cientos de tiles), en lugar de `*_row_fn` (que solo
    // admite 256 tiles por glyph×variant). Requiere planes=6 (EHB).
    const eng::u8* indexed_tiles = nullptr; // tilebank crudo (stride fijo, 1 B/píxel)
    eng::u32 indexed_stride = 0;            // bytes POR TILE (201: 16*16 = 256)
    // Banco de bloques YA interleaved X-Limited, producido en el host
    // (tools/amiga-tiles/amiga-tiles.mjs --xlimited) e incbinado en .MEMF_CHIP.
    // Evita la doble copia raw+interleaved en la Chip RAM del A500. Si se
    // suministra, `m_tiles[0]` (PF1) lo ALIA sin reservar arena (no cuenta como
    // copia en Chip RAM). El banco debe estar generado para `planes` (p. ej. 3
    // en DPF 8+8); ya no se exige planes==6 (EHB). En DPF homogéneo, PF2 usa su
    // propio `blocks_prebuilt2` si se da; si no, reutiliza `blocks_prebuilt`.
    const eng::u8* blocks_prebuilt = nullptr;
    eng::u32 blocks_prebuilt_size = 0;
    const eng::u8* blocks_prebuilt2 = nullptr;   // PF2 (DPF) si difiere del PF1
    eng::u32 blocks_prebuilt2_size = 0;

    // --- Composición (dual playfield / capas) -------------------------------
    XlimitedDualConfig dpf {};
    // --- Algoritmo (parámetros del scroll del playfield) --------------------
    eng::u8 max_step = 1;            // salto máx. px/frame por eje (ambos playfields).
                                     // El scroll se ejecuta como sub-pasos atómicos de
                                     // 1 px (paint-then-advance): nunca a medio pintar.
                                     // (por-playfield se ajusta con
                                     //  XLimitedPlayfield::set_scroll_step)
    bool scroll_y = true;            // corkscrew: display_height = viewport_h + 2*tile_height
    bool linear_display = false;     // display LINEAL sin split (espejo del bucle): elimina la
                                     // limitación del comparador de 8 bits a costa de 2x blits
    eng::field::ScrollMode scroll_mode = eng::field::ScrollMode::EightWay; // especialización del scroll

    // --- Conductor de validación (harness de las 8 direcciones) -------------
    XlimitedPathConfig path {};

    // --- Paleta --------------------------------------------------------------
    const eng::u16* palette = nullptr; // 2^planes colores (single) o 16 (DPF: PF1 0..7, PF2 8..15)
    eng::u32 copper_bytes = 1536;

    // --- Sprites hardware (a nivel de escena) ------------------------------
    eng::u32 sprite_data_bytes = 0;   // 0 = sin sprites; si > 0, reserva DATA Chip
};

/// Escena corkscrew reutilizable: uno o dos `XlimitedField` + compositor.
///
/// Uso típico:
///   XlimitedScene<ScrollConsts{16,16,kDh,kDph,4}> scene;  // geometría NTTP
///   scene.begin(memory, cfg);
///   scene.fill(backend, plan);          // relleno inicial (lotes)
///   scene.pre_scroll(backend, px_x, px_y);
///   // por frame:
///   scene.update_auto(plan, frame_index);   // o scene.update(plan, dx, dy)
///   backend.execute_frame_plan(plan);
///   scene.compose();
///   scene.install(backend);
///
/// `SC` (constantes a priori) se reenvían a los `XLimitedPlayfield<>` y de ahí
/// al `ScrollEngine`: hacen que las divisiones calientes del scroll usen
/// `fast_div` (sin `__udivsi3`). `ScrollConsts{}` (default) = geometría runtime.
template <ScrollConsts SC = ScrollConsts{}>
class XlimitedScene {
public:
    XlimitedScene() = default;
    XlimitedScene(const XlimitedScene&) = delete;
    XlimitedScene& operator=(const XlimitedScene&) = delete;

    bool begin(MemorySystem& memory, const XlimitedSceneConfig& cfg) {
        m_cfg = cfg;
        if (cfg.planes == 0 || cfg.planes > 6) return false;
        if (cfg.blocks_prebuilt == nullptr && cfg.blocks_prebuilt2 == nullptr &&
            cfg.fg_row_fn == nullptr && cfg.indexed_tiles == nullptr) return false;
        if (cfg.palette == nullptr) return false;
        if (cfg.map.width == 0 && (cfg.map.wrap_x == 0 && cfg.map.wrap_y == 0)) return false;
        // Main viewport: el HUD se resta del total. El WAIT de la zona HUD cae en
        // `DIWSTRT_y + main`; el comparador del Copper es de 8 bits, así que debe
        // ser <= 255. Con HUD la ventana DIW queda abierta al total (main + hud).
        const eng::u16 main_h = static_cast<eng::u16>(cfg.viewport_h - cfg.hud.height);
        const bool hud_zone = cfg.hud.height != 0;
        if (hud_zone) {
            if (cfg.hud.height > cfg.viewport_h) return false;
            if (static_cast<eng::u16>(xlimited_detail::kDiwStrt >> 8u) + main_h > 255u) return false;
            // El lienzo del HUD se reserva con el layout de DISPLAY del campo
            // corkscrew (misma profundidad `cfg.planes` y filas de viewport_w +
            // guarda de fetch), para que la zona overlay del Copper solo conmute
            // BPLxPT a mitad de frame sin reprogramar BPLCON0/DDF/BPLMOD (ver
            // XlimitedDisplayComposer::emit_full). La guarda izquierda de fetch
            // (16 px con fetch normal) queda fuera de la ventana visible; el HUD
            // dibuja su contenido desplazado esa guarda.
            const eng::u16 hud_w = static_cast<eng::u16>(
                cfg.viewport_w + (cfg.fetch_mode == 0u ? xlimited_detail::kExtraW32
                                                       : xlimited_detail::kExtraW64));
            if (!m_hud.begin(memory, {hud_w, cfg.hud.height, cfg.planes})) return false;
        }
        const eng::u8 n = static_cast<eng::u8>(cfg.dpf.enabled && !cfg.dpf.fg_canvas ? 2 : 1);
        const eng::u16 tw = cfg.tile_width, th = cfg.tile_height;
        for (eng::u8 pf = 0; pf < n; ++pf) {
            // Banco de bloques de este playfield (PF0/PF1 o PF2 según `pf`):
            //   - PRE-CONSTRUIDO real (aliado, sin copia): `blocks_prebuilt` para
            //     PF0 y `blocks_prebuilt2` para PF1 (si falta, PF1 reusa PF0).
            //   - tilebank INDEXADO real (convierte índices a `planes` planos).
            //   - generativo (row_fn simbólico, ≤256 tiles por glyph×variant).
            const bool isPf0 = (pf == 0u);
            const eng::u8* pb = isPf0 ? cfg.blocks_prebuilt
                : (cfg.blocks_prebuilt2 != nullptr ? cfg.blocks_prebuilt2 : cfg.blocks_prebuilt);
            const eng::u32 pbSize = (pb == cfg.blocks_prebuilt2)
                ? cfg.blocks_prebuilt2_size : cfg.blocks_prebuilt_size;
            if (pb != nullptr) {
                // Alia la región incbin (no propietaria): no reserva Chip RAM. El
                // banco se generó en el host para `cfg.planes` planos (DPF: 3) con
                // layout X-Limited de 320 px; el engine solo lo direcciona.
                m_tiles[pf].data = const_cast<void*>(static_cast<const void*>(pb));
                m_tiles[pf].size = pbSize;
                m_tiles[pf].kind = eng::MemoryKind::Chip;
            } else if (isPf0 && cfg.indexed_tiles != nullptr) {
                if (cfg.planes != 6) return false; // el pipeline EHB es 6 planos
                m_tiles[pf] = xlimited_build_blocks_bitmap_from_indexed(
                    memory, cfg.planes, tw, th, cfg.tileset_count,
                    cfg.indexed_tiles, cfg.indexed_stride);
            } else {
                const BlocksRowFn fn = isPf0 ? cfg.fg_row_fn : cfg.bg_row_fn;
                if (fn == nullptr) return false; // cada campo necesita una fuente
                m_tiles[pf] = xlimited_build_blocks_bitmap(
                    memory, cfg.planes, tw, th, cfg.tileset_count, fn);
            }
            if (!m_tiles[pf].valid()) return false;
            // Config del campo.
            XlimitedConfig fc;
            fc.map = (pf == 0) ? cfg.map : (cfg.map2.cells.empty() ? cfg.map : cfg.map2);
            fc.tileset = static_cast<const eng::u16*>(m_tiles[pf].data);
            fc.tileset_count = cfg.tileset_count;
            fc.planes = cfg.planes;
            fc.tile_width = tw;
            fc.tile_height = th;
            fc.viewport_w = cfg.viewport_w;
            fc.viewport_h = main_h; // el campo principal solo ve el main
            // El ANILLO del corkscrew se dimensiona para el viewport TOTAL (más el
            // staging de 2 bloques), NO para `main_h`: el HUD reduce solo el área
            // VISIBLE, pero el walk plane-shifted del scroll horizontal necesita el
            // anillo completo (18 bloques) para no colisionar `mapy` (hasta 17).
            fc.display_height = static_cast<eng::u16>(
                cfg.viewport_h + 2u * th);
            fc.screens_x = 16;
            fc.screens_y = 16;
            fc.scroll_y = cfg.scroll_y;
            fc.scroll_mode = cfg.scroll_mode;
            fc.linear_display = cfg.linear_display;
            fc.max_step = cfg.max_step;
            fc.bitmap_width = cfg.bitmap_width;
            fc.visible_tile_bias_x = cfg.visible_tile_bias_x;
            fc.visible_tile_bias_y = cfg.visible_tile_bias_y;
            fc.fetch_mode = cfg.fetch_mode;
            if (!m_field[pf].begin(memory, fc)) return false;
        }
        // Compositores. Con franja HUD la ventana DIW queda abierta al TOTAL
        // (main + hud); la zona HUD solo cambia BPLxPT/modulos en su raster.
        const u16 diwstop = hud_zone
            ? xlimited_detail::diwstop_for_viewport(cfg.viewport_h)
            : xlimited_detail::diwstop_for_viewport(main_h);
        const graphics::SpriteManager* sprites =
            (cfg.sprite_data_bytes != 0) ? &m_sprites : nullptr;
        if (cfg.dpf.enabled) {
            if (!m_dual.init(memory, {cfg.palette, cfg.copper_bytes, cfg.planes, false,
                xlimited_detail::kDiwStrt, diwstop,
                xlimited_detail::kDdfStrt, xlimited_detail::kDdfStop})) return false;
        } else {
            if (!m_single.init(memory, {cfg.palette, cfg.copper_bytes, cfg.planes,
                xlimited_detail::kDiwStrt, diwstop,
                xlimited_detail::kDdfStrt, xlimited_detail::kDdfStop,
                sprites})) return false;
        }
        if (cfg.sprite_data_bytes != 0) {
            if (!m_sprites.init(memory, cfg.sprite_data_bytes)) return false;
        }
        // FG como lienzo plano (DPF heterogéneo): el BG es el corkscrew, el FG un
        // CanvasPlayfield estático de viewport_w × viewport_h y `planes` bitplanes.
        if (cfg.dpf.enabled && cfg.dpf.fg_canvas) {
            if (!m_fg_canvas.begin(memory, {cfg.viewport_w, cfg.viewport_h, cfg.planes})) return false;
        }
        m_phase_frame = 0;
        m_phase = cfg.path.start_phase; // fase inicial del ciclo (update_auto)
        m_initialized = true;
        return true;
    }

    /// Rellena la pantalla inicial (y el PF2 si dual) en lotes. Devuelve false
    /// si un plan no se pudo encolar o ejecutar.
    template <typename Backend>
    bool fill(Backend& backend, graphics::FramePlan& plan) {
        const eng::u8 n = fields();
        for (eng::u8 pf = 0; pf < n; ++pf) {
            plan.clear();
            plan.set_blit_budget_limits({8192, 16384, 4, 120});
            const eng::u16 cols = m_field[pf].bitmap_blocks_per_row();
            const eng::u16 rows = m_cfg.scroll_y ? m_field[pf].display_blocks_per_col()
                : static_cast<eng::u16>(m_cfg.viewport_h / m_cfg.tile_height);
            for (eng::u16 b = 0; b < rows; ++b) {
                for (eng::u16 a = 0; a < cols; ++a) {
                    if (plan.blit_job_count() >= 120) {
                        if (!backend.execute_frame_plan(plan)) return false;
                        plan.clear();
                        plan.set_blit_budget_limits({8192, 16384, 4, 120});
                    }
                    // add_draw espeja el bloque en modo lineal (2 jobs por bloque).
                    if (!m_field[pf].add_draw(plan, a * m_cfg.tile_width,
                        b * m_field[pf].block_planes_lines(), a, b)) return false;
                }
            }
            if (plan.blit_job_count() > 0) {
                if (!backend.execute_frame_plan(plan)) return false;
                plan.clear();
            }
        }
        return true;
    }

    /// Pre-scrolla los playfields hacia delante (derecha/abajo) para dar
    /// recorrido a las direcciones reversas. Ejecuta los planes en lotes.
    template <typename Backend>
    bool pre_scroll(Backend& backend, graphics::FramePlan& plan, eng::s32 px_x, eng::s32 px_y) {
        plan.clear();
        plan.set_blit_budget_limits({8192, 16384, 4, 120});
        const eng::u8 n = fields();
        for (eng::s32 i = 0; i < px_x; ++i) {
            for (eng::u8 pf = 0; pf < n; ++pf) {
                if (!m_field[pf].scroll_right(plan)) return false;
            }
            if (plan.blit_job_count() >= 120) {
                if (!backend.execute_frame_plan(plan)) return false;
                plan.clear();
                plan.set_blit_budget_limits({8192, 16384, 4, 120});
            }
        }
        for (eng::s32 i = 0; i < px_y; ++i) {
            for (eng::u8 pf = 0; pf < n; ++pf) {
                if (!m_field[pf].scroll_down(plan)) return false;
            }
            if (plan.blit_job_count() >= 120) {
                if (!backend.execute_frame_plan(plan)) return false;
                plan.clear();
                plan.set_blit_budget_limits({8192, 16384, 4, 120});
            }
        }
        if (plan.blit_job_count() > 0) {
            if (!backend.execute_frame_plan(plan)) return false;
            plan.clear();
        }
        return true;
    }

    /// Desplazamiento explícito de 1 px (o 0) por eje, para un juego. Aplica el
    /// parallax configurado al PF2. Devuelve false si un borde bloqueó el avance.
    bool update(graphics::FramePlan& plan, eng::s32 dx, eng::s32 dy, eng::u32 frame) {
        const eng::u8 n = fields();
        // Ambos playfields con el mismo paso (parallax opcional en X).
        // Cada playfield aplica su salto (≤ su max_step) como sub-pasos atómicos
        // de 1 px (paint-then-advance) → nunca muestra columnas/filas a medias.
        const bool pf2_x = !m_cfg.dpf.parallax_x || (frame % m_cfg.dpf.parallax_x_div) == 0u;
        for (int axis = 0; axis < 2; ++axis) {
            const bool isH = (axis == 0);
            const eng::s32 step = isH ? dx : dy;
            if (step == 0) continue;
            for (eng::u8 pf = 0; pf < n; ++pf) {
                if (isH && pf == 1 && !pf2_x) continue; // parallax en X del PF2
                const eng::s32 ddx = isH ? step : 0;
                const eng::s32 ddy = isH ? 0 : step;
                if (!m_field[pf].update_scroll(plan, ddx, ddy)) return false;
            }
        }
        return true;
    }

    /// Avanza el camino configurado (1 px/frame según `effect`/`phase`). En las
    /// fases con componente negativa y diagonales el scroll inverso queda
    /// bloqueado en el borde 0 (no falla). Devuelve el dx/dy aplicados por referencia.
    /// La fase del ciclo se avanza con un contador incremental (sin `frame /
    /// phase_frames` por frame) y la tabla de Lissajous es constexpr: el dedo
    /// principal NO paga divisiones.
    bool update_auto(graphics::FramePlan& plan, eng::u32 frame, eng::s32& dx, eng::s32& dy) {
        dx = 0; dy = 0;
        eng::u32 phase;
        if (m_cfg.path.effect != 0) {
            phase = static_cast<eng::u32>(m_cfg.path.effect - 1);
        } else {
            // Equivalente a floor(frame/phase_frames) módulo 8 (mismo instante de
            // transición), sin división por frame: contador por fase.
            const eng::u32 pff = m_cfg.path.phase_frames ? m_cfg.path.phase_frames : 1u;
            if (m_phase_frame >= pff) { m_phase_frame = 0u; m_phase = static_cast<eng::u8>((m_phase + 1u) & 7u); }
            ++m_phase_frame;
            phase = m_phase;
        }
        switch (phase) {
            case 0: dx = 1; dy = 0; break;
            case 1: dx = -1; dy = 0; break;
            case 2: dx = 0; dy = 1; break;
            case 3: dx = 0; dy = -1; break;
            case 4: dx = (frame & 1u) ? 1 : 0; dy = (frame & 1u) ? 0 : 1; break;
            case 5: dx = (frame & 1u) ? -1 : 0; dy = (frame & 1u) ? 0 : -1; break;
            case 6: case 7: {
                const eng::u8 fx = static_cast<eng::u8>((phase == 7u) ? (frame + 32u) : frame);
                const eng::s32 sx = kSin[fx];
                const eng::s32 sy = kSin[kLissY[fx & 63u]];
                dx = (sx > 20) ? 1 : (sx < -20 ? -1 : 0);
                dy = (sy > 20) ? 1 : (sy < -20 ? -1 : 0);
                if (dx == 0 && dy == 0) dx = 1;
                break;
            }
            default: dx = 1; dy = 0; break;
        }
        return update(plan, dx, dy, frame);
    }

    /// Genera la copperlist del frame (single o dual) a partir del estado actual
    /// de los campos. Llámala tras ejecutar el plan. Con franja HUD, el
    /// compositor emite la zona inferior (playfield separado) tras el main.
    bool compose() {
        if (!m_initialized) return false;
        if (m_cfg.dpf.enabled) {
            if (m_cfg.dpf.fg_canvas) {
                return m_dual.compose(m_field[0].hardware_view(), m_fg_canvas.hardware_view());
            }
            return m_dual.compose(m_field[0].hardware_view(), m_field[1].hardware_view());
        }
        if (m_cfg.hud.height != 0) {
            const XlimitedDisplayComposer::OverlayZone hud {
                m_hud.hardware_view(),
                m_cfg.hud.palette,
                static_cast<eng::u8>(1u << m_cfg.hud.planes),
            };
            return m_single.compose(m_field[0].hardware_view(), &hud);
        }
        return m_single.compose(m_field[0].hardware_view());
    }

    template <typename Backend>
    void install(Backend& backend) const {
        if (!m_initialized) return;
        if (m_cfg.dpf.enabled) m_dual.install(backend);
        else m_single.install(backend);
    }

    constexpr bool ok() const { return m_initialized; }

    /// Reinicia el auto-ciclo de fases (para el CICLO DE VIDA al conmutar
    /// técnica: la fase vuelve a start_phase y el contador a 0).
    void reset_auto() {
        m_phase_frame = 0;
        m_phase = m_cfg.path.start_phase;
    }
    constexpr eng::u8 fields() const {
        return (m_cfg.dpf.enabled && !m_cfg.dpf.fg_canvas) ? 2u : 1u;
    }
    constexpr const XlimitedSceneConfig& config() const { return m_cfg; }
    constexpr u16 copper_words() const {
        return m_cfg.dpf.enabled ? m_dual.copper_words() : m_single.copper_words();
    }
    /// Depuración: puntero al copper activo y palabras usadas.
    const u16* debug_active_copper() const {
        return m_cfg.dpf.enabled ? nullptr : m_single.debug_active_copper();
    }
    /// ¿El split es siempre esperable con esta configuración (viewport <= 215)?
    /// Si es true, `linear_display` es innecesario: el modo split canónico usa
    /// 1 blit por operación y no tiene artefacto.
    constexpr bool split_always_waitable() const {
        return m_field[0].split_always_waitable();
    }

    // -------------------------------------------------------------------------
    // Acceso por ROL a los playfields (nunca por índice). Las primitivas de
    // dibujo pertenecen a cada playfield: `bg().set_pixel(...)`,
    // `fg().add_world_bitmap(...)`, etc. `fg` solo existe en modo dual.
    // -------------------------------------------------------------------------
    XLimitedPlayfield<SC>& bg() { return m_field[0]; }
    const XLimitedPlayfield<SC>& bg() const { return m_field[0]; }
    /// Segundo XLimited (DPF 3+3 homogéneo). En `fg_canvas` usa `canvas_fg()`.
    XLimitedPlayfield<SC>& fg() { return m_field[1]; }
    const XLimitedPlayfield<SC>& fg() const { return m_field[1]; }
    /// FG como lienzo plano (DPF heterogéneo: `dual && fg_canvas`). Dibuja aquí
    /// (una vez en init) con las primitivas; el contenido es estático.
    CanvasPlayfield& canvas_fg() { return m_fg_canvas; }
    const CanvasPlayfield& canvas_fg() const { return m_fg_canvas; }
    /// Sprites hardware (a nivel de escena): delante de los playfields, paleta
    /// COLOR16-31. Configura con `set(u8, SpriteConfig)`; la DATA en `sprite_data()`.
    graphics::SpriteManager& sprites() { return m_sprites; }
    const graphics::SpriteManager& sprites() const { return m_sprites; }
    /// Superficie de dibujo sobre el playfield de scroll (coordenadas de mundo;
    /// para objetos fijos convierte con `screen_to_world_x/y`). Es el contexto de
    /// dibujo con clip; `Playfield` no dibuja.
    Surface bg_surface() {
        return Surface(m_field[0], {0, 0, m_field[0].width(), m_field[0].display_height()});
    }
    /// Superficie de dibujo sobre el lienzo HUD (coordenadas de lienzo = pantalla).
    Surface hud_surface() {
        return Surface(m_hud, {0, 0, m_hud.width(), m_hud.height()});
    }
    /// Superficie de dibujo sobre el FG lienzo (DPF heterogéneo).
    Surface canvas_fg_surface() {
        return Surface(m_fg_canvas, {0, 0, m_fg_canvas.width(), m_fg_canvas.height()});
    }
    /// Playfield del HUD (lienzo plano en la franja inferior). Solo válido si
    /// `cfg.hud.height > 0`. Dibuja aquí (una vez en init) con las primitivas.
    CanvasPlayfield& hud() { return m_hud; }
    const CanvasPlayfield& hud() const { return m_hud; }
    constexpr bool has_hud() const { return m_cfg.hud.height != 0; }

private:
    /// Tabla de seno Q8 generada en COMPILE-TIME (patrón `LissYTable`): cambiar
    /// la amplitud o el número de pasos es solo instanciar otro `SineTable<Amp>`.
    /// Sustituye al array de 64 valores escritos a mano (mismos valores a ±1).
    static constexpr eng::SineTable<64> kSin {};

    /// Tabla constexpr de `(i*7/10)&63` para la fase Lissajous: evita la
    /// división `/10` por frame (se calcula en compile-time con `ct_array`).
    static constexpr eng::ct_array<eng::u8, 64> kLissY {
        [](eng::usize i) { return static_cast<eng::u8>((static_cast<eng::u32>(i) * 7u) / 10u) & 63u; }
    };

    XlimitedSceneConfig m_cfg {};
    XLimitedPlayfield<SC> m_field[2] {};
    CanvasPlayfield m_hud {};        // franja HUD (lienzo plano, si hud_height>0)
    CanvasPlayfield m_fg_canvas {};  // FG lienzo plano (DPF heterogéneo)
    graphics::SpriteManager m_sprites {};
    MemoryBlock m_tiles[2] {};
    XlimitedDisplayComposer m_single {};
    XlimitedDualComposer m_dual {};
    bool m_initialized = false;
    eng::u32 m_phase_frame = 0;      // frames transcurridos en la fase actual
    eng::u8 m_phase = 0;             // fase activa del ciclo (0..7)
};

} // namespace eng::field
