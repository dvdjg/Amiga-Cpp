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
// Configuración de la demo 107 — X-Limited puro (single playfield)
// -----------------------------------------------------------------------------
//
// El flujo es el mismo que en Steger:
//
//   CPU: decide mapposx/videoposx y DDF/BPLCON1
//        |                |
//        +--> Blitter: 1 blit de BLOCKPLANELINES = kTileSize*kPlanes líneas por píxel de scroll
//             (ej. 16*3=48, 16*4=64, 16*5=80, 16*6=96 según K_PLANES 3..6)
//        +--> Copper: BPLxPT + BPLCON1/BPLMOD sin split (genérico bytes*planes)
//
// La CPU nunca mueve la ventana manualmente: el wrap vertical del fetch
// lineal hace que BITMAPWIDTH+ 2 bytes sea la siguiente planelínea.

#ifndef K_TILE_WIDTH
#define K_TILE_WIDTH 16
#endif
#ifndef K_FETCH_MODE
#define K_FETCH_MODE 0
#endif
#ifndef K_PLANES
#define K_PLANES 4 // default 4 planos; válido 3|4|5|6 (8/16/32/64 colores, DPF 3+3 para 6)
#endif

constexpr eng::u16 kTileWidth = static_cast<eng::u16>(K_TILE_WIDTH);
constexpr eng::u8 kFetchMode = static_cast<eng::u8>(K_FETCH_MODE);
constexpr eng::u16 kTileSize = 16;
constexpr eng::u8 kPlanes = static_cast<eng::u8>(K_PLANES);
constexpr eng::u16 kViewportW = 320;
constexpr eng::u16 kViewportH = 256;
constexpr eng::u16 kMapTilesX = 256;
constexpr eng::u16 kMapTilesY = 128;
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
    // Scroll continuo: 2 px/frame a la derecha, infinito por wrap del mapa.
    eng::s32 scroll_accum = 0;

    void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
        eng::debug::mark_init_started(g_eng_run_status);
        if (!backend.configure_memory({360u * 1024u, 16u * 1024u, 8u * 1024u})) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010701u);
            return;
        }

        // Banco de tiles en formato clásico de Steger: BlocksBitmap interleaved
        // 320×256×planes (40*256*planes bytes, planes 3..6 → 40*256*3/4/5/6).
        // Cada tile de 16×16 ocupa BLOCKPLANELINES = kTileSize*kPlanes planelíneas
        // interleaved (ej. 48/64/80/96 para 3/4/5/6 planos). No usamos el layout
        // TileMajor de build_tile_cache porque X-Limited hace un único blit
        // interleaved de BLOCKPLANELINES líneas (ver xlimited.hpp §6). Creamos el
        // BlocksBitmap interleaved y lo rellenamos con pf_plane_row.
        const eng::u32 kBlockSrcWidth = 320;
        const eng::u32 kBlockSrcBytesPerRow = kBlockSrcWidth / 8; // 40
        const eng::u32 kBlocksPerRowSrc = kBlockSrcWidth / kTileWidth; // 20 para 16, 10 para 32
        const eng::u32 kBlocksPerColSrc = 256 / kTileSize; // 16
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

        // Para el banco interleaved de Steger necesitamos un bitmap de
        // 320×256 interleaved (BlocksBitmap). Nuestro tiles_block ya contiene
        // los tiles en layout [tile][plane][row][word]; lo adaptamos creando
        // un buffer interleaved lineal de 40*256*planes bytes y copiando cada
        // tile a su posición (block %20, block/20). Para simplificar la demo
        // y mantener el contrato draw_block idéntico al original, rellenamos
        // un buffer temporal interleaved y lo usamos como blocksbuffer.
        // Si la memoria es escasa, reutilizamos tiles_block como si ya fuera
        // el BlocksBitmap: el offset de bloque sigue siendo block%20*2 +
        // block/20*BLOCKPLANELINES*40, y como nuestros tiles están en orden
        // lineal por índice, ese offset coincide con tile*planes*words.
        // Por tanto podemos apuntar directamente a tiles_block.
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
        field_cfg.bitmap_width = 352; // 352 para K_TILE_WIDTH=16 → 22 bloques
        if (kTileWidth == 32) {
            // Con tiles de 32, 352 no es múltiplo de 32 (352/32=11). Es válido
            // pero desperdicia medio bloque. Para mantener el contrato
            // BITMAPBLOCKSPERROW entero, la demo fuerza 384 con tiles de 32.
            field_cfg.bitmap_width = 384;
        }
        // Fetch ancho explícito: fuerza 384 px y el modo pedido.
        // 0=16 px normal (352), 1=BPL32 32 px (384, DDF $28/$C8, offset 16),
        // 3=BPL32+BPAGEM 64 px (384, DDF $18/$B8, offset 48).
        // K_FETCH_MODE !=0 implica bitmap 384 independientemente de K_TILE_WIDTH.
        if (kFetchMode != 0) {
            field_cfg.bitmap_width = 384;
            field_cfg.fetch_mode = kFetchMode;
        } else {
            field_cfg.fetch_mode = 0;
        }

        if (!field.begin(backend.memory(), field_cfg)) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010703u);
            return;
        }

        // Fill inicial por Blitter en lotes. X-Limited necesita 22×16=352
        // bloques (352 jobs) y FramePlan solo admite 128 jobs (max_blit_jobs)
        // y el presupuesto por defecto es max_jobs=120. Si dejamos que
        // add_tile_block_copy falle por overflow, FramePlan::m_ok pasa a false
        // y el siguiente execute_frame_plan(plan) fallaría aunque los 128
        // jobs previos fueran válidos. Por eso vaciamos *antes* de superar
        // max_jobs, no después de que add haya marcado el plan como no ok.
        plan.clear();
        plan.set_blit_budget_limits({8192, 16384, 4, 120});
        {
            const eng::u16 cols = field.bitmap_blocks_per_row();
            const eng::u16 rows = kViewportH / kTileSize;
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

        // Scroll X infinito: 1 píxel por frame para 50 fps sin micro-parones.
        // Cada píxel es exactamente 1 blit de BLOCKPLANELINES = kTileSize*kPlanes
        // planelíneas (48/64/80/96 según planes 3..6, §5). Con 1 px/frame
        // el fine scroll avanza 1/16 por frame y BPLCON1 cicla 0x00→0xFF sin saltos.
        // Con 2 px/frame el avance era 2/16, visible como micro-parón cada 8 frames
        // y como BPLCON1 duplicado; además duplicaba la carga de Blitter y el
        // riesgo de no terminar antes del siguiente VBlank. Mantener 1 px/frame
        // es la forma canónica de Steger (ver xlimited.c: main_loop con 1 iter).
        const int steps = 1;
        for (int i = 0; i < steps; ++i) {
            // Si llegamos al límite del mapa lógico, hacemos wrap a 0 y
            // refilleamos la pantalla para mantener la ilusión de infinito
            // sin salir del área extra del bitmap. El coste es un fill de
            // 352 bloques, amortizado una vez cada 4096/2=2048 frames (~40s).
            const eng::s32 limit = static_cast<eng::s32>(kMapTilesX) * kTileWidth -
                                   kViewportW - kTileWidth;
            if (field.mapposx() >= limit) {
                // Wrap brusco pero sin tearing visible: la siguiente columna
                // entrante ya es la del principio del mapa gracias al wrap del
                // TileLayerMap. Reiniciar videoposx evita que planeaddx crezca
                // sin bound y salga de la altura extra.
                // Hacemos un reset suave: no re-reservamos Chip, sólo
                // reiniciamos coordenadas y refilleamos la parte visible.
                // Para la demo, marcamos failed y dejamos que el watchdog del
                // runner lo detecte como loop; en una implementación real se
                // haría un cross-fade o se mantendría videoposx modular.
                // Aquí simplemente envolvemos mapposx/videoposx a 0 mediante
                // un nuevo begin (barato: no reasigna si cabe).
                // Simplificación: dejamos que mapposx siga creciendo y el
                // TileLayerMap haga wrap en el índice; el planeaddx sigue
                // creciendo linealmente pero la altura extra para 256 bloques
                // es 262, que da 512 bytes de planeaddx máximo, suficiente
                // para cubrir 256*16=4096 píxeles (4096/16*2=512). Justo al
                // límite. Si seguimos más allá, el planeaddx saldría del
                // bitmap; por eso envolvemos aquí.
                // Wrap infinito sin re-reservar: reiniciamos coordenadas.
                // El mapa es circular (wrap_x), así que los tiles siguen
                // coincidiendo; el planeaddx vuelve a 0 y la altura extra
                // cubre el siguiente ciclo.
                field.reset_scroll();
                // Refill ligero: re-pintar las 22 columnas visibles en y=0
                // es suficiente para que el siguiente fetch no vea basura.
                // Para la demo hacemos un fill parcial de la primera fila.
                plan.clear();
                plan.set_blit_budget_limits({8192, 16384, 4, 120});
                for (eng::u16 a = 0; a < field.bitmap_blocks_per_row(); ++a) {
                    auto job = field.draw_block_job(
                        a * kTileWidth, 0, a, 0);
                    if (!plan.add_tile_block_copy(job)) {
                        if (!backend.execute_frame_plan(plan)) {
                            ready = false;
                            eng::debug::mark_failed(g_eng_run_status, 0x0001070au);
                            return;
                        }
                        plan.clear();
                        plan.set_blit_budget_limits({8192, 16384, 4, 120});
                        plan.add_tile_block_copy(job);
                    }
                }
                if (plan.blit_job_count() > 0) {
                    if (!backend.execute_frame_plan(plan)) {
                        ready = false;
                        eng::debug::mark_failed(g_eng_run_status, 0x0001070au);
                        return;
                    }
                    plan.clear();
                    plan.set_blit_budget_limits({8192, 16384, 4, 120});
                }
                break;
            }
            if (!field.scroll_right(plan)) {
                ready = false;
                eng::debug::mark_failed(g_eng_run_status, 0x0001070bu);
                return;
            }
        }

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
