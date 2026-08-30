#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/field/tile_demo.hpp>
#include <eng/field/xlimited.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/core/span.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
    eng::debug::run_status_magic,
    eng::debug::run_status_version,
    static_cast<eng::u16>(eng::debug::RunState::Cold),
    0,
    0,
};
}

namespace {

namespace field = eng::field;
namespace demo = eng::field::demo;

// -----------------------------------------------------------------------------
// Configuración de la demo 107 — X-Limited puro (single playfield) parametrizada
// -----------------------------------------------------------------------------
//
// El flujo es el mismo que en Steger:
//
//   CPU: decide mapposx/videoposx y DDF/BPLCON1
//        |                |
//        +--> Blitter: 1 blit de BLOCKPLANELINES = tile_h*planes líneas por píxel de scroll
//             (ej. 16*3=48, 16*4=64, 16*5=80, 16*6=96 según K_PLANES 3..6)
//        +--> Copper: BPLxPT + BPLCON1/BPLMOD sin split (genérico bytes*planes)
//
// La CPU nunca mueve la ventana manualmente: el wrap vertical del fetch
// lineal hace que BITMAPWIDTH+ 2 bytes sea la siguiente planelínea.
// Viewport y espacio virtual parametrizados: K_VIEWPORT_W/H (defecto 320/256,
// alternativo 288/224) y K_SCREENS_X/Y (16×16 pantallas → mapa screens*viewport/tile).

#ifndef K_TILE_WIDTH
#define K_TILE_WIDTH 16
#endif
#ifndef K_TILE_SIZE
#define K_TILE_SIZE 16
#endif
#ifndef K_FETCH_MODE
#define K_FETCH_MODE 0
#endif
#ifndef K_PLANES
#define K_PLANES 4 // default 4 planos; válido 3|4|5|6 (8/16/32/64 colores, DPF 3+3 para 6)
#endif
#ifndef K_VIEWPORT_W
#define K_VIEWPORT_W 320
#endif
#ifndef K_VIEWPORT_H
#define K_VIEWPORT_H 256
#endif
#ifndef K_SCREENS_X
#define K_SCREENS_X 16
#endif
#ifndef K_SCREENS_Y
#define K_SCREENS_Y 16
#endif
// Alias K_TILE_W/H para la parametrización nueva (compatibles con K_TILE_WIDTH/SIZE)
#ifndef K_TILE_W
#ifdef K_TILE_WIDTH
#define K_TILE_W K_TILE_WIDTH
#else
#define K_TILE_W 16
#endif
#endif
#ifndef K_TILE_H
#ifdef K_TILE_SIZE
#define K_TILE_H K_TILE_SIZE
#else
#define K_TILE_H 16
#endif
#endif

constexpr eng::u16 kTileWidth = static_cast<eng::u16>(K_TILE_W);
constexpr eng::u16 kTileSize = static_cast<eng::u16>(K_TILE_H);
constexpr eng::u8 kFetchMode = static_cast<eng::u8>(K_FETCH_MODE);
constexpr eng::u8 kPlanes = static_cast<eng::u8>(K_PLANES);
constexpr eng::u16 kViewportW = static_cast<eng::u16>(K_VIEWPORT_W);
constexpr eng::u16 kViewportH = static_cast<eng::u16>(K_VIEWPORT_H);
constexpr eng::u8 kScreensX = static_cast<eng::u8>(K_SCREENS_X);
constexpr eng::u8 kScreensY = static_cast<eng::u8>(K_SCREENS_Y);
// 16×16 pantallas virtuales → mapa en tiles derivado de viewport/tile
constexpr eng::u16 kMapTilesX = static_cast<eng::u16>(K_SCREENS_X * (K_VIEWPORT_W / K_TILE_W));
constexpr eng::u16 kMapTilesY = static_cast<eng::u16>(K_SCREENS_Y * (K_VIEWPORT_H / K_TILE_H));
constexpr eng::u8 kTileCount = 64;

eng::u16 g_map_cells[kMapTilesX * kMapTilesY] {};

void build_map(eng::Span<eng::u16> cells, eng::u32 seed) {
    for (eng::u32 y = 0; y < kMapTilesY; ++y) {
        for (eng::u32 x = 0; x < kMapTilesX; ++x) {
            const eng::u32 h = demo::cell_hash(x, y, seed);
            cells.at(y * kMapTilesX + x) = static_cast<eng::u16>(h & 63u);
        }
    }
}

struct DemoGame {
    eng::MemoryBlock tiles_block {};
    field::XlimitedField field {};
    field::XlimitedDisplayComposer composer {};
    eng::graphics::FramePlan plan {};
    field::XlimitedConfig field_cfg {};
    bool ready = false;
    // Scroll continuo: 1 px/frame a la derecha, infinito por wrap del mapa.
    eng::s32 scroll_accum = 0;

    void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
        eng::debug::mark_init_started(g_eng_run_status);
        if (!backend.configure_memory({360u * 1024u, 16u * 1024u, 8u * 1024u})) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010701u);
            return;
        }

        // Banco de tiles en formato clásico de Steger: BlocksBitmap interleaved
        // viewport_w × viewport_h × planes genérico (ej. 320×256 → 40*256*planes).
        // Cada tile de kTileSize×kTileWidth ocupa BLOCKPLANELINES = kTileSize*kPlanes planelíneas
        // interleaved (ej. 48/64/80/96 para 3/4/5/6 planos). No usamos el layout
        // TileMajor de build_tile_cache porque X-Limited hace un único blit
        // interleaved de BLOCKPLANELINES líneas (ver xlimited.hpp §6). Creamos el
        // BlocksBitmap interleaved y lo rellenamos con pf_plane_row.
        const eng::u32 kBlockSrcWidth = 320;
        const eng::u32 kBlockSrcBytesPerRow = kBlockSrcWidth / 8; // 40
        const eng::u32 kBlocksPerRowSrc = kBlockSrcWidth / kTileWidth; // 20 para 16, 10 para 32
        const eng::u32 blocks_interleaved_bytes = kBlockSrcBytesPerRow * 256 * kPlanes;
        tiles_block = backend.memory().chip.allocate(blocks_interleaved_bytes, 16);
        if (!tiles_block.valid()) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010702u);
            return;
        }
        // Inicializar a 0
        for (eng::u32 i = 0; i < blocks_interleaved_bytes; ++i) static_cast<eng::u8*>(tiles_block.data)[i] = 0;
        // Rellenar cada tile del tileset (64) en su posición (bx,by) = (tile%20, tile/20)
        for (eng::u16 tile = 0; tile < kTileCount; ++tile) {
            const eng::u8 glyph = static_cast<eng::u8>(tile & 15u);
            const eng::u8 variant = static_cast<eng::u8>((tile >> 4u) & 3u);
            const eng::u16 bx = tile % kBlocksPerRowSrc;
            const eng::u16 by = tile / kBlocksPerRowSrc;
            const eng::u32 base_pl = static_cast<eng::u32>(by) * (kTileSize * kPlanes) * kBlockSrcBytesPerRow;
            for (eng::u16 row = 0; row < kTileSize; ++row) {
                for (eng::u8 plane = 0; plane < kPlanes; ++plane) {
                    const eng::u16 word = demo::pf_plane_row(glyph, variant, static_cast<eng::u8>(row), plane, 0, false);
                    // Para tiles anchos (>16), replicar patrón en las words siguientes
                    for (eng::u16 w = 0; w < kTileWidth / 16u; ++w) {
                        const eng::u16 out_word = (w == 0) ? word : static_cast<eng::u16>(word ^ 0x0ff0u);
                        const eng::u32 planeline = static_cast<eng::u32>(row) * kPlanes + plane;
                        const eng::u32 dst_offset = (base_pl + planeline * kBlockSrcBytesPerRow) + bx * (kTileWidth / 8u) + w * 2u;
                        // Escribir big-endian word en buffer Chip (Amiga es big-endian)
                        eng::u8* dst = static_cast<eng::u8*>(tiles_block.data) + dst_offset;
                        dst[0] = static_cast<eng::u8>(out_word >> 8);
                        dst[1] = static_cast<eng::u8>(out_word & 0xff);
                    }
                }
            }
        }

        // Para el banco interleaved de Steger necesitamos un BlocksBitmap
        // interleaved (ej. 320×256 para el banco origen, genérico si se cambia viewport).
        // Nuestro tiles_block ya contiene los tiles en layout [tile][plane][row][word];
        // el offset de bloque sigue siendo block%20*2 + block/20*BLOCKPLANELINES*40,
        // y como nuestros tiles están en orden lineal por índice, ese offset coincide
        // con tile*planes*words. Por tanto podemos apuntar directamente a tiles_block.
        build_map(eng::Span<eng::u16>::from_raw(g_map_cells, kMapTilesX * kMapTilesY), 0x13579bdu);

        field_cfg.map.cells = eng::Span<const eng::u16>::from_raw(g_map_cells, kMapTilesX * kMapTilesY);
        field_cfg.map.width = kMapTilesX;
        field_cfg.map.height = kMapTilesY;
        field_cfg.map.wrap_x = kMapTilesX;
        field_cfg.map.wrap_y = kMapTilesY;
        field_cfg.map.edge_tile = 0;
        field_cfg.tileset = static_cast<const eng::u16*>(tiles_block.data);
        field_cfg.tileset_count = kTileCount;
        field_cfg.planes = kPlanes;
        field_cfg.tile_width = kTileWidth;
        field_cfg.tile_height = kTileSize;
        field_cfg.viewport_w = kViewportW;
        field_cfg.viewport_h = kViewportH;
        field_cfg.screens_x = kScreensX;
        field_cfg.screens_y = kScreensY;
        field_cfg.scroll_y = true; // 16×16 pantallas: columnas de 17 tiles para V (16 visibles +1 guarda)
        field_cfg.scroll_y = false;
        // bitmap_width auto: viewport_w + EXTRAWIDTH (32 para fetch normal, 64 para fetch ancho)
        // Se deja 0 para que XlimitedField lo derive; alternativa explícita:
        // field_cfg.bitmap_width = kViewportW + (kFetchMode==0?32:64) → 352/384 para 320, 320/352 para 288
        field_cfg.bitmap_width = 0;
        field_cfg.fetch_mode = kFetchMode;

        if (!field.begin(backend.memory(), field_cfg)) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010703u);
            return;
        }

        // Fill inicial por Blitter en lotes. X-Limited necesita cols*rows jobs
        // (ej. viewport 320→22×16=352, viewport 288→20×14=280) y FramePlan solo admite 128 jobs
        // y el presupuesto por defecto es max_jobs=120. Vaciamos *antes* de superar max_jobs.
        plan.clear();
        plan.set_blit_budget_limits({8192, 16384, 4, 120});
        {
            const eng::u16 cols = field.bitmap_blocks_per_row();
            const eng::u16 visibleRows = static_cast<eng::u16>(kViewportH / kTileSize);
            const eng::u16 colHeight = static_cast<eng::u16>(visibleRows + (field_cfg.scroll_y ? 1u : 0u));
            const eng::u16 rows = colHeight;
            for (eng::u16 b = 0; b < rows; ++b) {
                for (eng::u16 a = 0; a < cols; ++a) {
                    // Vaciar proactivamente si estamos al límite de jobs.
                    if (plan.blit_job_count() >= 120) {
                        if (!backend.execute_frame_plan(plan)) {
                            eng::debug::mark_failed(g_eng_run_status, 0x00010705u);
                            return;
                        }
                        plan.clear();
                        plan.set_blit_budget_limits({8192, 16384, 4, 120});
                    }
                    auto job = field.draw_block_job(
                        a * kTileWidth,
                        b * field.block_planes_lines(),
                        a, b);
                    if (!plan.add_tile_block_copy(job)) {
                        // Fallo inesperado (job inválido) — no es por overflow
                        // porque ya vaciamos proactivamente.
                        eng::debug::mark_failed(g_eng_run_status, 0x00010706u);
                        return;
                    }
                }
            }
            if (plan.blit_job_count() > 0) {
                if (!backend.execute_frame_plan(plan)) {
                    eng::debug::mark_failed(g_eng_run_status, 0x00010705u);
                    return;
                }
                plan.clear();
            }
        }

        // Paleta por defecto de tile_demo: 2^planes colores (8/16/32/64 para 3/4/5/6 planos)
        // En 4 planos son 16 colores; genérico usa `1u << kPlanes` entradas de la paleta.
        if (!composer.init(backend.memory(), {demo::kPalette, 1536, kPlanes})) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010707u);
            return;
        }
        auto view = field.hardware_view();
        if (!composer.compose(view)) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010708u);
            return;
        }
        composer.install(backend);

        ready = true;
        eng::debug::mark_ready(g_eng_run_status, 0x10700000u);
    }

    void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
        eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
        if (!ready) return;

        plan.clear();
        plan.set_blit_budget_limits({8192, 16384, 4, 120});

        // Ciclo preparado para los siguientes retos: horizontal → vertical → diagonal → sinusoidal.
        // Cada fase dura 300 frames (~6 s a 50 fps) y usa 1 px/frame para mantener
        // 50 fps sin micro-parones. Cada píxel es como máximo 1 blit de
        // BLOCKPLANELINES = kTileSize*kPlanes planeline (48/64/80/96 según planes 3..6, §5).
        // Con 1 px/frame el fine scroll avanza 1/tile_width por frame y BPLCON1 cicla
        // sin saltos; con 2 px/frame se veía micro-parón cada 8 frames.
        // Orden actual: H primero para validar sin negro inicial (V deja 17ª fila
        // con inner-black 0.08→0.38 en los primeros 16 px hasta que la fila se
        // completa). Tras fijar el pre-fill de la fila 17, se cambia a V primero.
        const eng::u32 phase = (context.frame.frame_index / 1000u) % 4u;
        // 0:H, 1:V, 2:HV, 3:diagonal — tras validar V, cambiar a 0:V
        const bool doH = (phase==0) || (phase==2) || (phase==3);
        const bool doV = (phase==1) || (phase==2) || (phase==3);
        // Sinusoidal modula la dirección dentro de la fase 3
        eng::s32 dx = 0, dy = 0;
        if (phase==3) {
            // Lissajous simple: X = sin(frame), Y = cos(frame*0.7) → -1,0,1
            const eng::s32 sx = demo::sin64(static_cast<eng::u8>(context.frame.frame_index & 63));
            const eng::s32 sy = demo::sin64(static_cast<eng::u8>((context.frame.frame_index*7/10) & 63));
            dx = (sx > 20) ? 1 : (sx < -20 ? -1 : 0);
            dy = (sy > 20) ? 1 : (sy < -20 ? -1 : 0);
            if (dx==0 && dy==0) dx = 1; // asegurar avance mínimo
        } else {
            dx = doH ? 1 : 0;
            dy = doV ? 1 : 0;
            // En fase diagonal, alternar H/V por frame para no hacer 2 blits/frame
            // y mantener 50 fps incluso con 6 planes (96 planeline → 2 blits =192 líneas).
            if (phase==2 && (context.frame.frame_index & 1u)) { dx=1; dy=0; } else if (phase==2) { dx=0; dy=1; }
        }
        // Ejecutar los desplazamientos necesarios para este frame (1 blit por eje como máximo)
        // Orden: primero H luego V para que saveword se gestione por eje.
        for (int axis=0; axis<2; ++axis) {
            const bool isH = (axis==0);
            const eng::s32 step = isH ? dx : dy;
            if (step==0) continue;
            if (isH && step>0) {
                // Horizontal derecha 1 px
            } else if (isH && step<0) {
                // Horizontal izquierda (no usado en ciclo actual, preparado para DPF)
            }
            // El bucle steps se mantiene para compatibilidad con el código de wrap
            const int steps = 1;
            for (int i = 0; i < steps; ++i) {
                if (isH) {
                    const eng::s32 limit = static_cast<eng::s32>(kMapTilesX) * kTileWidth - kViewportW - kTileWidth;
                    if (field.mapposx() >= limit) {
                        field.reset_scroll();
                        plan.clear();
                        plan.set_blit_budget_limits({8192, 16384, 4, 120});
                        for (eng::u16 a = 0; a < field.bitmap_blocks_per_row(); ++a) {
                            auto job = field.draw_block_job(a * kTileWidth, 0, a, 0);
                            if (!plan.add_tile_block_copy(job)) {
                                if (!backend.execute_frame_plan(plan)) { ready=false; eng::debug::mark_failed(g_eng_run_status, 0x0001070au); return; }
                                plan.clear(); plan.set_blit_budget_limits({8192, 16384, 4, 120});
                                plan.add_tile_block_copy(job);
                            }
                        }
                        if (plan.blit_job_count()>0) { if (!backend.execute_frame_plan(plan)) { ready=false; eng::debug::mark_failed(g_eng_run_status, 0x0001070au); return; } plan.clear(); plan.set_blit_budget_limits({8192, 16384, 4, 120}); }
                        break;
                    }
                    bool ok = (step>0) ? field.scroll_right(plan) : field.scroll_left(plan);
                    if (!ok) { ready=false; eng::debug::mark_failed(g_eng_run_status, 0x0001070bu); return; }
                } else {
                    // Vertical: usar mapposy/videoposy, con wrap en Y
                    const eng::s32 limitY = static_cast<eng::s32>(kMapTilesY) * kTileSize - kViewportH - kTileSize;
                    if (step>0 && field.mapposy() >= limitY) { field.reset_scroll(); break; }
                    if (step<0 && field.mapposy() <= 0) { /* no wrap hacia arriba en demo cíclica */ break; }
                    bool ok = (step>0) ? field.scroll_down(plan) : field.scroll_up(plan);
                    if (!ok) { ready=false; eng::debug::mark_failed(g_eng_run_status, 0x0001070bu); return; }
                }
            }
        } // axis H/V

        if (!backend.execute_frame_plan(plan)) {
            ready = false;
            eng::debug::mark_failed(g_eng_run_status, 0x0001070cu);
            return;
        }

        auto view = field.hardware_view();
        if (!composer.compose(view)) {
            ready = false;
            eng::debug::mark_failed(g_eng_run_status, 0x0001070du);
            return;
        }

        // Telemetría: mapposx en bytes bajos, videoposx en altos, BPLCON1 en medio.
        const eng::u32 marker = 0x10700000u |
            (static_cast<eng::u32>(field.mapposx() & 0xff) ) |
            (static_cast<eng::u32>(view.bplcon1 & 0xff) << 8) |
            (static_cast<eng::u32>(field.videoposx() & 0xff) << 16);
        eng::debug::mark_ready(g_eng_run_status, marker);
    }

    void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
        if (ready) composer.install(backend);
        eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
    }
};

DemoGame g_game {};

} // namespace

int main() {
    SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
    eng::debug::reset(g_eng_run_status);

    eng::amiga::MinimalBackend backend {};
    eng::Engine engine { backend, g_game };
    engine.run_frames(0xffffffffu);

    return 0;
}
