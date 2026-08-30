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
//        +--> Blitter: 1 blit de 64 líneas por píxel de scroll
//        +--> Copper: BPLxPT + BPLCON1/BPLMOD sin split
//
// La CPU nunca mueve la ventana manualmente: el wrap vertical del fetch
// lineal hace que BITMAPWIDTH+ 2 bytes sea la siguiente planelínea.

#ifndef K_TILE_WIDTH
#define K_TILE_WIDTH 16
#endif
#ifndef K_PLANES
#define K_PLANES 4
#endif

constexpr eng::u16 kTileWidth = static_cast<eng::u16>(K_TILE_WIDTH);
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

        // Banco de tiles en formato clásico de Steger (320×256, 20×16 bloques).
        // Reusamos demo::build_tile_cache que genera el mismo patrón que 106
        // pero lo dejamos en un bloque Chip contiguo para que DrawBlock pueda
        // calcular mapy_src = (block/20)*BLOCKPLANELINES*40.
        const eng::u32 words_per_tile = kTileSize * (kTileWidth / 16u);
        tiles_block = backend.memory().chip.allocate(
            kTileCount * kPlanes * words_per_tile * 2u, 16);
        if (!tiles_block.valid()) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010702u);
            return;
        }
        // Base 0, sin transparencia total: X-Limited puro muestra todos los tiles.
        demo::build_tile_cache(
            eng::Span<eng::u16>::from_raw(
                static_cast<eng::u16*>(tiles_block.data),
                tiles_block.size / sizeof(eng::u16)),
            kTileCount, kTileSize, kTileWidth, kPlanes, 0, false, -1);

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
        field_cfg.fetch_mode = 0;

        if (!field.begin(backend.memory(), field_cfg)) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010703u);
            return;
        }

        // Fill inicial por Blitter en lotes (como en 106). El primer frame
        // visible nunca muestra la superficie a medio rellenar.
        const eng::u8 budget = 120;
        plan.clear();
        plan.set_blit_budget_limits({8192, 16384, 4, 120});
        if (!field.fill_screen(plan)) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010704u);
            return;
        }
        // Ejecutar todos los jobs de fill en bucle hasta vaciar.
        // En esta demo fill_screen emite todos de golpe (22*16=352 jobs) y el
        // FramePlan tiene max 128 jobs, de modo que iteramos por columnas.
        // Simplificación: rellenamos por bandas de 32 bloques.
        {
            // Si el plan se llenó, lo ejecutamos y seguimos; como fill_screen
            // es atómico en esta implementación simple, el caso >128 no ocurre
            // para 22*16=352 >128. Dividimos en dos pasadas.
            eng::graphics::FramePlan tmp;
            tmp.clear();
            // Pasada 1: primeras 128
            // Para no complicar, si el plan falló por overflow lo partimos
            // manualmente: rellenamos fila a fila con pump.
            if (!plan.ok()) {
                plan.clear();
                plan.set_blit_budget_limits({8192, 16384, 4, 120});
                // Fallback: fill fila a fila
                const eng::u16 cols = field.bitmap_blocks_per_row();
                const eng::u16 rows = kViewportH / kTileSize;
                for (eng::u16 b = 0; b < rows; ++b) {
                    for (eng::u16 a = 0; a < cols; ++a) {
                        auto job = field.draw_block_job(
                            a * kTileWidth,
                            b * field.block_planes_lines(),
                            a, b);
                        if (!plan.add_tile_block_copy(job)) {
                            if (!backend.execute_frame_plan(plan)) {
                                eng::debug::mark_failed(g_eng_run_status, 0x00010705u);
                                return;
                            }
                            plan.clear();
                            plan.set_blit_budget_limits({8192, 16384, 4, 120});
                            if (!plan.add_tile_block_copy(job)) {
                                eng::debug::mark_failed(g_eng_run_status, 0x00010706u);
                                return;
                            }
                        }
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

        // Paleta por defecto de tile_demo (16 colores para 4 planos)
        // En 4 planos usamos los primeros 16 colores de la paleta de 32.
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

        // Scroll X infinito: 2 píxeles por frame. Cada píxel es un blit de
        // un bloque (ver §5). Con 2 px/frame, 2 blits por frame.
        const int steps = 2;
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
                field = field::XlimitedField{};
                field_cfg.map.cells = eng::Span<const eng::u16>::from_raw(g_map_cells, kMapTilesX * kMapTilesY);
                if (!field.begin(backend.memory(), field_cfg)) {
                    ready = false;
                    eng::debug::mark_failed(g_eng_run_status, 0x00010709u);
                    return;
                }
                plan.clear();
                plan.set_blit_budget_limits({8192, 16384, 4, 120});
                // Fill por bandas para no overflowar el plan
                const eng::u16 cols = field.bitmap_blocks_per_row();
                const eng::u16 rows = kViewportH / kTileSize;
                for (eng::u16 b = 0; b < rows; ++b) {
                    for (eng::u16 a = 0; a < cols; ++a) {
                        auto job = field.draw_block_job(
                            a * kTileWidth,
                            b * field.block_planes_lines(),
                            a, b);
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
