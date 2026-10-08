// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/218_free_form_sprite_layer --debug
//               bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/218_free_form_sprite_layer --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/218_free_form_sprite_layer --release
//
// ============================================================================
// Demo 218 — «SPR Layer» (Jeroen Knoester, 2018): recreación fiel en el engine
// ============================================================================
//
// Port funcional 1:1 de la demo de referencia `spr_layer/Sprite_Layer` (código 68000
// original en `SPR_Layer.asm`, `GFX/layer.asm`, `GFX/tilemap.asm`, `GFX/blitter.asm`,
// `Data/copperlists.asm`). No es una variante: se transcriben el mismo reparto de
// trabajo por frames, las mismas copperlists, los mismos blits y los mismos datos.
//
// QUÉ SE VE (idéntico a la referencia)
//   - **Capa de fondo free-form** (sprite-as-playfield): 19 columnas de 16 px = 304 px
//     no repetitivos, dibujadas por los 8 canales de sprite. 8 columnas las pinta el
//     **DMA** (estructuras de sprite de 224 líneas) y 11 el **Copper** rearmando los
//     canales dentro de la línea (`SPRxPOS`+`SPRxDATB`+`SPRxDATA`, sin `SPRxCTL`).
//     Va **detrás** del playfield (BPLCON2=$0000).
//   - **Playfield de 4 planos** (15 colores) con scroll horizontal de 1 px/frame:
//     el puntero de planos avanza 2 bytes cada 16 frames y `BPLCON1` da el pixel fino
//     (nibble alto y bajo iguales). Los tiles de 32x32 entran por la derecha dibujados
//     por el Blitter **repartidos en 28 frames** (un cuarto de tile por frame).
//   - **9 BOBs** de 32x32 (4 planos) con cookie-cut `$CA` y restore desde el tercer
//     buffer, rebotando en Y entre 16 y 224; los pares/impares van a `y` y `224-y`.
//   - **Sub-buffer** de 3 planos (288x16) en las líneas 269..284 con su propia paleta.
//   - Pantalla de título con texto de 8x8 (fuente de la referencia) y espera del botón
//     izquierdo del ratón, como el `DBGPause` original.
//
// POR QUÉ ES EXACTO (transcripción, no reinterpretación)
//   - `UpdateSprCtl`: cada 2 frames alterna `SPRxCTL` ($0c03/$0c02) → pixel impar.
//   - `UpdateLayerPos`: reparte las 19 posiciones en **4 frames** (cuartos de columna)
//     sobre la copperlist que se mostrará en el siguiente bloque de 4.
//   - `UpdateLayerData`: reparte la DATA en **32 frames** (canales 0..7 por DMA en los
//     frames 0..7, columnas Copper 1..11 en 8..18 y la segunda lista en 19..29; el
//     frame 30 retrocede 18 columnas el offset del tilemap).
//   - 4 copperlists (`clist1..4`) con doble juego (2 pares) y doble buffer de
//     estructuras de sprite (`spr0..7`/`spr0b..7b`); `COP1LC` se escribe por frame sin
//     `COPJMP` (el Copper recarga en el VBlank).
//   - 3 buffers de foreground (`fg_buf1..3`, 352x338 px, 4 planos intercalados):
//     1/2 se alternan cada 256 frames y el 3º es la copia limpia para el restore.
//
// CLÁUSULAS DEL HARDWARE QUE SOSTIENEN EL EFECTO (verificadas en la fuente del
// emulador, `../WinUAE-DBG/`):
//   - La comparación vertical del sprite usa VSTART/VSTOP de 8 bits con **wrap**:
//     `sprite_0_height = vstop - vstart` (`custom.cpp:4479`); el estado de DMA se arma
//     al pasar por la línea `vstart` y se desarma en `vstop` (`custom.cpp:10065-10077`).
//     Por eso las estructuras usan VSTART=44 y VSTOP=12 → 224 líneas.
//   - Escribir `SPRxPOS` a media línea **no desarma** el sprite si `vstart` no coincide
//     con la línea actual (`sprstartstop`, `custom.cpp:4018-4030`): el rearmado Copper
//     puede usar `VSTART=$7f` para las columnas y seguir pintando.
//   - `SPRxDATA` es quien **arma** el comparador horizontal (AHRM cap. 4, «Manual
//     Mode»); por eso el rearmado escribe POS+DATB+DATA y **nunca** CTL.
//   - El HSTART del sprite es `(pos&0xff)*2 + (ctl&1)` (`drawing.cpp:2706`): el bit 0
//     de `SPRxCTL` da el píxel impar.
//
// Referencias: `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`,
// `docs/reference/amiga/techniques/sprite-layer.md`, AHRM 3.ª cap. 4 y 6.
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/debug/peripheral.hpp> // presupuesto por elemento del bucle (contadores 0-4)
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/hw/bus_budget.hpp> // presupuesto de bus declarado (dimensión vertical incluida)
#include <eng/os/os.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

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

// --- Assets de la referencia (Turrican II; ver assets/amiga/sprites/spr_layer/README.md) ---
// Tiles y BOBs los lee el **Blitter**, así que en `init` se copian a Chip RAM. La fuente
// de 8x8 solo la lee la CPU (PlotChar), así que se usa directamente desde el binario.
INCBIN(spr_bgtiles, "assets/amiga/sprites/spr_layer/bg_tiles_spr.raw");
INCBIN(spr_fgtiles, "assets/amiga/sprites/spr_layer/fg_tiles.raw");
INCBIN(spr_sbtiles, "assets/amiga/sprites/spr_layer/sb_tiles.raw");
INCBIN(spr_bobs, "assets/amiga/sprites/spr_layer/bob_4bpl.raw");
INCBIN(spr_masks, "assets/amiga/sprites/spr_layer/mask_4bpl.raw");
INCBIN(spr_font, "assets/amiga/sprites/spr_layer/font8x8.raw");

namespace {

using eng::s16;
using eng::u16;
using eng::u32;
using eng::u8;
using eng::usize;

// ============================================================================
// Constantes (transcripción literal de la referencia)
// ============================================================================

// --- Capa de sprites (`Data/copperlists.i`, `GFX/layer.i`) ---
constexpr u16 kSprFirstLine = 0x2c;   // VWAIT: primera línea de la capa (44)
constexpr u16 kLayerLines = 224;      // col_height: líneas que cubre la capa
constexpr u16 kCols = 19;             // 8 columnas DMA + 11 columnas Copper
constexpr u16 kDmaCols = 8;
constexpr u16 kCopperCols = kCols - kDmaCols; // 11
constexpr u16 kSprPos = 0x48;         // SPRPOS: HSTART de la 1.ª columna (X=144)
constexpr u16 kSprRPos = 0x7f00 | (kSprPos + 0x40); // SPRRPOS = $7f88 (columnas Copper)
constexpr u16 kDmaPos = 0x2c00 | kSprPos;           // DMAPOS = $2c48 (DMA/reposición)
constexpr u16 kDmaRPos = 0x8000 | kSprPos;          // DMARPOS = $8048 (valor inicial)
constexpr u16 kSprCtl = 0x0c02;       // VSTOP=12: 224 líneas por wrap (12-44 mod 256)
constexpr u16 kHWait = 0x49;          // HWAIT: palabra de WAIT con H=72 px lo-res
constexpr u16 kWaitHArg = kHWait * 2; // `wait_raw` recibe H en lo-res px (la divide /2)
constexpr u16 kLineWords = 84;        // palabras por línea del efecto (42 instrucciones)
constexpr u16 kSprColMod = 166;       // DMOD de los blits de columnas (168-2 bytes)
constexpr u16 kLayer1Lines = 212;     // primeras 212 líneas (VP=$2c..$ff)
constexpr u16 kLayer2Lines = 12;      // últimas 12 (VP=$00..$0b → líneas 256..267)
constexpr u32 kSprStructWords = 516;  // [POS,CTL,224×(DATA,DATB),0,0]
constexpr u32 kSprStructBytes = kSprStructWords * 2u; // 1032

// BPLCON0/BPLCON2/DDF/DIW del display principal (`Data/copperlists.asm`).
constexpr u16 kDiwstrt = 0x2c92;      // ventana 288x241
constexpr u16 kDiwstop = 0x1db1;
constexpr u16 kDdfstrt = 0x0038;      // 19 palabras = 304 px por plano
constexpr u16 kDdfstop = 0x00c8;
constexpr u16 kBplcon0 = 0x4200;      // 4 planos, lo-res, color

// ============================================================================
// Presupuesto de bus declarado (medido; campaña de rendimiento, F3-B)
// ============================================================================
// Display 224 líneas × (refresh 4 + sprites 16 + bitplanes 80 = 100) + Copper (42 MOVE/línea
// ≈ 19,2k slots, medido 19.160-19.226). Blitter por coste constexpr del frame más caro
// (fills + staging + tiles + restores + BOBs) y CPU medida (slots de bus por update).
// Datos y método: docs/debugging/investigaciones/218-campana-rendimiento.md y
// docs/engine/architecture/BUS_BUDGET.md §Reparto vertical.
constexpr eng::hw::BusCost kSceneBusCost =
	eng::hw::copper_cost(9408u, 128u) +          // 42 MOVE × 224 líneas + WAITs
	eng::hw::blit_cost(19u * 224u, 1u) +         // fills de posiciones (A = cero)
	eng::hw::blit_cost(2u * 224u, 3u) +          // staging lineal de columnas
	eng::hw::blit_cost(3u * 128u, 3u) +          // tiles FG (3 buffers, 2 palabras × 64 líneas)
	eng::hw::blit_cost((15u + 12u) * 128u, 2u) + // restores (unión de las celdas de cada fila)
	eng::hw::blit_cost(9u * 3u * 128u, 4u) +     // 9 BOBs cookie-cut (A+B+C+D)
	eng::hw::cpu_cost(19700u);                   // CPU medida (9.859 slots/campo × 2)

[[nodiscard]] constexpr eng::hw::BusBudgetInput scene_bus_input() noexcept {
	eng::hw::BusBudgetInput in {};
	in.bands_count = 1u;
	in.bands[0].height = kLayerLines; // 224 líneas de DMA (la ventana del efecto)
	in.bands[0].width = 320u;         // ancho de fetch: 20 palabras/plano
	in.bands[0].bitplanes = 4u;
	in.bands[0].sprites_active = 8u;
	return eng::hw::with_cost(in, kSceneBusCost);
}

// Tripwire de compilación (campaña F4 abierta): el Blitter (27.488 slots) supera el hueco de
// VBlank (88 líneas × 215 = 18.920) y no hay trabajo planificado en él → los blits pesados corren
// en la ventana a ~mitad de velocidad. Al cerrar la campaña (ancla + orden), actualizar esta línea.
static_assert(eng::hw::amiga500_bus_budget(scene_bus_input()).vblank_free_slots == 18920u,
	      "hueco de VBlank medido: 88 líneas × 215 slots");
static_assert((eng::hw::amiga500_bus_budget(scene_bus_input()).hints & eng::hw::kHintBlitterInDisplay) != 0u,
	      "trampa conocida: Blitter > hueco de VBlank sin planificar (campaña F4 abierta)");

// --- Foreground de 4 planos (`GFX/displaybuffers.i`) ---
constexpr u16 kDisplayW = 288;
constexpr u16 kBufMod = 44;                     // bytes por fila de un plano (352 px)
constexpr u16 kBufLine = 176;                   // bytes por scanline (4 planos intercalados)
constexpr u16 kBufHeight = 338;                 // 224 + 64 + 50 (buffer_scroll_hgt)
constexpr u32 kBufPlaneBytes = (kDisplayW + 64u) * kBufHeight / 8u; // 14872
constexpr u32 kBufBytes = kBufPlaneBytes * 4u;  // 59488
constexpr u16 kFgMod = kBufLine;                // fg_mod = buffer_modulo*4
constexpr u16 kFgDispMod = (kBufMod * 3u) + (kBufMod - kDisplayW / 8u) - 2u; // 138
constexpr u16 kFgClearRows = kBufHeight * 2u;   // 676 filas físicas en el clear
constexpr u16 kFgScrollMax = (kDisplayW * 49u) / 8u; // 1764: fin del scroll
constexpr u16 kScrollPan = 2;                   // bytes que avanza el puntero cada 16 frames
constexpr u16 kFgTileRowWords = 32;             // palabras por fila del tilemap FG
constexpr u16 kFgTileBytes = 512;               // tile 32x32x4
constexpr u16 kFgHalfBytes = 256;               // 16 líneas (fg_thsize)
constexpr u16 kFgHalfY = kFgMod * 16u;          // 2816: 16 scanlines (fg_mod*ftile_height/2)

// --- Sub-buffer de 3 planos (`GFX/displaybuffers.i`) ---
constexpr u16 kSbMod = kDisplayW / 8u;          // 36 bytes por fila de plano
constexpr u32 kSbPlaneBytes = static_cast<u32>(kDisplayW) * 16u / 8u; // 576
constexpr u32 kSbBytes = kSbPlaneBytes * 3u;    // 1728
constexpr u16 kSbDispMod = kSbMod * 2u;         // 72
constexpr u16 kSbTileBytes = 96;                // tile 16x16x3
constexpr u16 kSbTiles = 18;                    // 288/16
constexpr u16 kSbTextOffset = kSbMod * 3u * 4u; // (subbuffer_modulo*3)*4 = 432: 4 líneas

// --- Tilemap de fondo (`GFX/tilemap.asm`) ---
constexpr u16 kBgTileBytes = 64;                // tile 16x16x2
constexpr u16 kBgTileRowWords = 32;             // 32 columnas
constexpr u16 kBgTileRows = 14;

constexpr u16 kBgTileMap[kBgTileRowWords * kBgTileRows] = {
	0x07u,0x00u,0x01u,0x09u,0x0au,0x02u,0x00u,0x00u,0x01u,0x02u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x03u,0x05u,0x04u,0x00u,0x06u,
	0x00u,0x03u,0x05u,0x05u,0x05u,0x05u,0x04u,0x03u,0x05u,0x05u,0x04u,0x01u,0x02u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x03u,0x05u,0x05u,0x05u,0x04u,0x00u,
	0x03u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x01u,0x02u,0x00u,0x00u,0x00u,0x06u,0x07u,0x00u,0x00u,0x00u,0x00u,0x03u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,
	0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x03u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,
	0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,0x00u,0x03u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,
	0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x0cu,0x00u,0x01u,0x02u,0x0bu,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,
	0x00u,0x00u,0x01u,0x02u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x01u,0x02u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x03u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
	0x00u,0x0bu,0x08u,0x08u,0x0cu,0x00u,0x00u,0x00u,0x00u,0x0bu,0x08u,0x08u,0x0cu,0x01u,0x02u,0x01u,0x09u,0x0au,0x09u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
	0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x01u,0x02u,0x03u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x06u,0x07u,0x00u,0x00u,0x00u,
	0x00u,0x06u,0x07u,0x00u,0x00u,0x00u,0x00u,0x03u,0x0au,0x09u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
	0x00u,0x00u,0x00u,0x00u,0x03u,0x0au,0x09u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,0x00u,
	0x00u,0x00u,0x01u,0x09u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,
	0x00u,0x03u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,
	0x0bu,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x0cu,0x00u,0x00u,
};

// --- Tilemap de foreground (`GFX/tilemap.asm`, 7 filas de 32 columnas) ---
constexpr u16 kFgTileMap[kFgTileRowWords * 7u] = {
	0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x21u,0x21u,0x21u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
	0x00u,0x00u,0x00u,0x36u,0x37u,0x00u,0x00u,0x00u,0x21u,0x21u,0x21u,0x00u,0x36u,0x37u,0x00u,0x00u,0x00u,0x36u,0x37u,0x00u,0x00u,0x00u,0x00u,0x22u,0x23u,0x00u,0x3cu,0x3du,0x00u,0x3cu,0x3du,0x00u,
	0x16u,0x00u,0x00u,0x38u,0x39u,0x00u,0x00u,0x00u,0x15u,0x15u,0x17u,0x00u,0x38u,0x39u,0x00u,0x00u,0x00u,0x38u,0x39u,0x00u,0x00u,0x00u,0x24u,0x25u,0x1eu,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
	0x15u,0x16u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x15u,0x17u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x1fu,0x27u,0x28u,0x22u,0x23u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
	0x15u,0x15u,0x16u,0x00u,0x00u,0x00u,0x00u,0x00u,0x17u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x1eu,0x26u,0x26u,0x26u,0x26u,0x0bu,0x0cu,0x0du,0x0bu,0x0cu,0x0du,0x00u,
	0x17u,0x17u,0x17u,0x14u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x14u,0x1eu,0x26u,0x26u,0x26u,0x26u,0x0au,0x0bu,0x0fu,0x10u,0x11u,0x0du,0x00u,
	0x20u,0x21u,0x20u,0x21u,0x22u,0x23u,0x00u,0x00u,0x24u,0x25u,0x27u,0x28u,0x00u,0x29u,0x00u,0x1fu,0x00u,0x1eu,0x00u,0x27u,0x28u,0x26u,0x26u,0x22u,0x23u,0x01u,0x02u,0x03u,0x04u,0x05u,0x29u,0x00u,
};

constexpr u16 kSbTileMap[kSbTiles] = {
	0x00u,0x01u,0x01u,0x01u,0x01u,0x01u,0x01u,0x01u,0x01u,0x01u,
	0x01u,0x01u,0x01u,0x01u,0x01u,0x01u,0x01u,0x03u,
};

// --- Paletas (`SPR_Layer.asm`): 0..15 FG, 16 transparente, 17..31 sprites ---
constexpr eng::Palette32 kPalette {{
	0x000u, 0x223u, 0x008u, 0x500u, 0x000u, 0x445u, 0xa21u, 0x382u,
	0x778u, 0xe50u, 0xaabu, 0xfa3u, 0x5dbu, 0xfffu, 0x0f8u, 0xbbbu,
	0x000u,
	0x22bu, 0x444u, 0xdddu, 0x000u, 0x22bu, 0x444u, 0xdddu,
	0x000u, 0x22bu, 0x444u, 0xdddu, 0x000u, 0x22bu, 0x444u, 0xdddu,
}};

constexpr u16 kSubPal[8] = {0x000u, 0x440u, 0x660u, 0x880u, 0xaa0u, 0xcc0u, 0xee0u, 0x000u};

// --- Texto de la referencia (`titletxt`/`subtxt`) ---
struct TextLine {
	u8 color;
	u8 x;
	u8 y;
	const char* text;
};

constexpr TextLine kTitleText[] = {
	{6u, 7u, 1u, "Free Form Sprite Layer"},
	{9u, 0u, 3u, "This program uses a custom copper "},
	{9u, 0u, 4u, "list to generate a free form sprite "},
	{9u, 0u, 5u, "layer."},
	{9u, 0u, 7u, "This layer is shown behind a 16 "},
	{9u, 0u, 8u, "colour foreground and allows for "},
	{9u, 0u, 9u, "a 15/4 colour dual layer screen "},
	{9u, 0u, 10u, "without restrictions on the content "},
	{9u, 0u, 11u, "of the background layer."},
	{9u, 0u, 13u, "On top of this effect, nine bobs "},
	{9u, 0u, 14u, "(32x32) are displayed."},
	{11u, 0u, 16u, "More info? See sourcecode or readme."},
	{9u, 0u, 18u, "Left mouse button exits back to OS."},
	{11u, 2u, 26u, "Press left mouse button to begin."},
};

constexpr TextLine kSubText[] = {
	{6u, 1u, 0u, "15 colour FG / 4 colour BG / 50Hz"},
};

// --- Índices de parcheo por copperlist (los registra `build_clist`) ---
struct ListLayout {
	u16 spr_ptr_word = 0;               // valor H del 1.er SPRxPT
	u16 bpl_ptr_word = 0;               // valor H de BPL1PTH (display principal)
	u16 shift_word = 0;                 // valor de BPLCON1 (display principal)
	u16 sb_ptr_word = 0;                // valor H de BPL1PTH del sub-buffer
	u16 pos_word[kCols] {};             // valor de cada SPRxPOS (línea 0)
	u16 datb_word[kCopperCols] {};      // valor DATB de cada columna Copper (línea 0)
};

constexpr usize kAssetBytes = 832u + 32256u + 384u + 512u + 512u;
constexpr u32 kAssetBgOff = 0u;
constexpr u32 kAssetFgOff = 832u;
constexpr u32 kAssetSbOff = 832u + 32256u;
constexpr u32 kAssetBobOff = 832u + 32256u + 384u;
constexpr u32 kAssetMaskOff = 832u + 32256u + 384u + 512u;

constexpr u8 kBobCount = 9;


/// Códigos de estado del run-status.
constexpr u32 kDetailBase = 0x00021800u;
constexpr u32 kDetailReady = kDetailBase;

struct SprLayerDemo {
	// ------------------------------------------------------------------
	// Ciclo de vida del juego
	// ------------------------------------------------------------------
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// 384K Chip: 3 buffers FG (178.5K) + 4 copperlists (152K) + 16 estructuras de
		// sprite (16.5K) + sub-buffer (1.7K) + assets DMA (34.5K) ≈ 383K.
		if (!backend.configure_memory({384u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, kDetailBase + 1u);
			return;
		}
		auto& chip = backend.memory_manager().chip();
		// Lista mínima para **congelar el SO** antes del trabajo pesado: el original lo
		// hace en su wrapper (`AllOff`: DMA e IRQ a cero) y sin ello la IRQ de VBlank del
		// SO puede reprogramar el Blitter en medio de un blit de la capa de init y dejar
		// columnas Copper a medias (visto: filas de tile distintas entre arranques).
		// La lista aparca el Copper en un WAIT que nunca dispara; la pantalla queda en
		// COLOR00 hasta el `takeover_display` final.
		m_minimal = chip.reserve<eng::CopperTag>(16u, 16u);
		u16* mini = m_minimal.view.as_words().data();
		mini[0] = 0xffffu; mini[1] = 0xfffeu;
		mini[2] = 0xffffu; mini[3] = 0xfffeu;
		backend.takeover_display(mini);
		for (u8 i = 0; i < 3; ++i) {
			m_fg[i] = chip.reserve<eng::PlaneTag>(kBufBytes, 16u);
		}
		for (u8 i = 0; i < 4; ++i) {
			m_clist[i] = chip.reserve<eng::CopperTag>(kClistBytes, 16u);
		}
		for (u8 i = 0; i < 2; ++i) {
			m_spr[i] = chip.reserve<eng::SpriteTag>(kDmaCols * kSprStructBytes, 16u);
		}
		m_sb = chip.reserve<eng::PlaneTag>(kSbBytes, 16u);
		m_assets = chip.reserve<eng::TextureTag>(static_cast<u32>(kAssetBytes), 16u);
		m_bg_stage = chip.reserve<eng::TextureTag>(kLayerLines * 4u, 16u);
		bool ok = m_sb.valid() && m_assets.valid() && m_minimal.valid() && m_bg_stage.valid();
		for (u8 i = 0; i < 3; ++i) { ok = ok && m_fg[i].valid(); }
		for (u8 i = 0; i < 4; ++i) { ok = ok && m_clist[i].valid(); }
		for (u8 i = 0; i < 2; ++i) { ok = ok && m_spr[i].valid(); }
		if (!ok) {
			eng::debug::mark_failed(g_eng_run_status, kDetailBase + 2u);
			return;
		}

		// Assets DMA a Chip (el Blitter no ve Slow/Fast): tiles de fondo/foreground, sub-buffer, BOB y máscara. La fuente 8x8 se queda en el binario (solo CPU).
		u8* a = m_assets.view.data();
		copy_bytes(a + kAssetBgOff, reinterpret_cast<const u8*>(spr_bgtiles), 832u);
		copy_bytes(a + kAssetFgOff, reinterpret_cast<const u8*>(spr_fgtiles), 32256u);
		copy_bytes(a + kAssetSbOff, reinterpret_cast<const u8*>(spr_sbtiles), 384u);
		copy_bytes(a + kAssetBobOff, reinterpret_cast<const u8*>(spr_bobs), 512u);
		copy_bytes(a + kAssetMaskOff, reinterpret_cast<const u8*>(spr_masks), 512u);
		// Vistas tipadas de cada asset dentro del bloque Chip (el tag es el del bloque; la vista es de solo lectura porque el Blitter solo las lee).
		m_bg_tiles = m_assets.view.as_const().subspan(kAssetBgOff, 832u);
		m_fg_tiles = m_assets.view.as_const().subspan(kAssetFgOff, 32256u);
		m_sb_tiles = m_assets.view.as_const().subspan(kAssetSbOff, 384u);
		m_bobs = m_assets.view.as_const().subspan(kAssetBobOff, 512u);
		m_masks = m_assets.view.as_const().subspan(kAssetMaskOff, 512u);
		m_font = reinterpret_cast<const u8*>(spr_font);

		// Ventanas de trabajo tipadas (equivalen a `clist_ptrs`/`sprset_ptrs`/`fg_buf1..3` del original): el dominio va en el tipo (copperlist/estructura de sprite/sub-buffer) y el banco lo garantiza el bloque Chip del que salen.
		for (u8 i = 0; i < 4; ++i) { m_clist_words[i] = m_clist[i].view.as_words(); }
		m_sprset_words[0] = m_spr[0].view.as_words();
		m_sprset_words[1] = m_spr[1].view.as_words();
		m_sb_words = m_sb.view.as_words();

		// Estructuras DMA iniciales: [POS, CTL, 224×($5555,$aaaa), 0,0] (`Data/sprites.asm`).
		for (u8 set = 0; set < 2; ++set) {
			for (u8 ch = 0; ch < kDmaCols; ++ch) {
				// Subvista de la estructura del canal dentro del juego (516 palabras): así las escrituras quedan indexadas por palabra, como en la referencia.
				eng::Words<eng::SpriteTag> s =
					m_sprset_words[set].subspan(static_cast<u32>(ch) * kSprStructWords, kSprStructWords);
				s[0] = static_cast<u16>(kDmaPos + ch * 8u);
				s[1] = kSprCtl;
				for (u16 l = 0; l < kLayerLines; ++l) {
					s[2u + l * 2u + 0u] = 0x5555u; // DATA
					s[2u + l * 2u + 1u] = 0xaaaau; // DATB
				}
				s[kSprStructWords - 2u] = 0u;
				s[kSprStructWords - 1u] = 0u;
			}
		}

		// Copperlist 1 (plantilla): cabecera, paletas, capa y sub-buffer.
		if (!build_clist(m_clist[0], m_lay)) {
			eng::debug::mark_failed(g_eng_run_status, kDetailBase + 3u);
			return;
		}
		// Punteros de la plantilla (`SetSPRPtrs`/`SetFGPtrs`/`SetSBPtrs` originales,
		// antes de copiar para que las 4 listas queden completas).
		m_sprshow = 0;
		set_spr_ptrs(m_clist_words[0]);
		set_fg_ptrs(0u, 0u, 2u); // estado inicial del título: buf1 + 2 (`moveq #2,d3`)
		set_sb_ptrs();

		// Relleno inicial de la DATA de la capa: 32 iteraciones de `UpdateLayerData`
		// (DMA 0..7 y columnas Copper 8..18/19..29); después la copia propaga clist1
		// a clist2..4 (como el `CopyMem` original).
		m_c32 = 0;
		for (u8 i = 0; i < 32u; ++i) {
			update_layer_data(backend);
			++m_c32;
		}
		m_c32 = 0;
		for (u8 i = 1; i < 4; ++i) {
			// Copia palabra a palabra entre vistas tipadas (equivale al CopyMem original).
			for (u32 w = 0; w < kClistWords; ++w) { m_clist_words[i][w] = m_clist_words[0][w]; }
		}
		// clist3/4 apuntan al segundo juego de estructuras (`spr0b..7b`).
		m_sprshow = 1;
		set_spr_ptrs(m_clist_words[2]);
		set_spr_ptrs(m_clist_words[3]);
		m_sprshow = 0;

		// Limpieza de los 3 buffers FG (676 filas × 44 palabras, DMOD=0).
		blitter_clear_buffer(backend);
		draw_sub_buffer(backend);
		backend.wait_blitter();

		// Texto del título en los 3 buffers (base + 4 bytes) y del sub-buffer (+3 líneas).
		// `row_stride` = `buffer_modulo*4` = kFgMod (paso de scanline intercalado).
		for (u8 i = 0; i < 3; ++i) {
			plot_text_multi(m_fg[i].view.data() + 4u, kFgMod, kBufMod, 4u, kTitleText,
					sizeof(kTitleText) / sizeof(TextLine));
		}
		plot_text_multi(m_sb_words.as_bytes().data() + kSbTextOffset, kSbMod * 3u, kSbMod, 3u,
				kSubText, sizeof(kSubText) / sizeof(TextLine));

		// Display: toma de control y arranque **inmediato** del efecto (sin esperar
		// eventos): la primera pantalla es el estado inicial del original (capa de
		// sprites + texto del título) y el scroll empieza en el primer frame.
		backend.takeover_display(m_clist_words[0].data()); // frontera pendiente: takeover_display aún recibe u16*
		start_main_loop();
		eng::debug::mark_ready(g_eng_run_status, kDetailReady);
	}

	/// `WaitRaster 0x2c` del original (`PhotonsMiniWrapper.asm:89-95`, llamada al inicio del
	/// bucle principal): espera a que el haz pase por la línea 44. Ancla la **fase** de cada
	/// iteración respecto al display. Sin ella, con el efecto ocupando más de un campo, el
	/// bucle encadena updates sin esperar al VBlank y las escrituras «vivas» del update
	/// (`SPRxCTL` de `UpdateSprCtl` y las `SPRxPOS` de las estructuras DMA del caso 3, que no
	/// van por la copperlist) caen en posiciones de haz distintas cada frame: barren la capa
	/// a media visualización y producen temblor. El original lo evita esperando siempre la
	/// misma línea.
	static void wait_raster_layer() {
		// VPOSR/VHPOSR como long en $dff004; línea = bits 8-0 tras `lsr.l #1 / lsr.w #7`.
		// Línea 300 (borde inferior, VBlank): los blits pesados del update caen en el hueco de
		// bus del borde y corren a ~2-3 ciclos/slot (medido con el grid DMA del frame profiler:
		// docs/debugging/investigaciones/218-campana-rendimiento.md). El original espera la 44;
		// la campaña F4 midió que la 300 baja el update de ~220k a ~170k ciclos.
		volatile const u32* const vpos = reinterpret_cast<volatile const u32*>(0x00dff004u);
		while (((*vpos >> 8u) & 0x1ffu) != 0x12cu) {
		}
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& c) {
		wait_raster_layer();
		const eng::u32 t0 = eng::debug::DebugPeripheral::cycle_counter();
		if (m_fg_offset == kFgScrollMax) {
			// El original deja de actualizar al llegar a las 49 pantallas.
			m_updates++;
			eng::debug::mark_frame(g_eng_run_status, m_updates);
			return;
		}
		update_spr_ctl();
		const eng::u32 t1 = eng::debug::DebugPeripheral::cycle_counter();
		// Restore de BOBs al principio (hueco de bus del borde; contador 7): limpia las celdas
		// del frame anterior antes de que los BOBs dibujen al final del update.
		restore_bobs(backend);
		update_layer_pos(backend);
		const eng::u32 t2 = eng::debug::DebugPeripheral::cycle_counter();
		update_layer_data(backend);
		const eng::u32 t3 = eng::debug::DebugPeripheral::cycle_counter();
		update_fg_tiles(backend);
		const eng::u32 t3b = eng::debug::DebugPeripheral::cycle_counter();
		update_counters();
		update_scroll();
		// Publica la copperlist del frame y parchea el display principal (`COP1LC` sin `COPJMP`: el Copper recarga en el VBlank, como el `move.l a2,cop1lc` original).
		const u8 idx = static_cast<u8>((m_clist_idx >> 2u) + (m_cshow_idx >> 2u));
		eng::Words<eng::CopperTag> list = m_clist_words[idx];
		backend.install_copper_list(list.data()); // frontera pendiente: install_copper_list aún recibe `u16*`
		list[m_lay.shift_word] = m_fg_shift;
		// Selector de buffer FG del original (`btst #0,c32frame_cn_o+1`, `SPR_Layer.asm:378-398`):
		// prueba el byte alto de un contador 0..31, que vale siempre 0 → dibuja y muestra
		// siempre `fg_buf2` (índice 1). Se replica tal cual: un doble buffer por update
		// introduce un salto periódico de 8 px del playfield cada 16 updates (medido contra
		// la referencia con el watchpoint de `COP1LC`), porque el avance grueso del puntero
		// (+2 B cada 16 updates) queda a caballo entre los dos bitmaps.
		const u8 buf = ((m_c32 & 0x100u) != 0u) ? 0u : 1u;
		set_fg_ptrs(idx, buf, m_fg_offset);
		const eng::u32 t4 = eng::debug::DebugPeripheral::cycle_counter();
		draw_bob_cells(backend);
		const eng::u32 t5 = eng::debug::DebugPeripheral::cycle_counter();
		// Presupuesto por elemento del bucle (contadores del periférico de depuración,
		// legibles con `run-demo.sh --read-debugperiph counters`): 0 = update completo,
		// 1 = UpdateSprCtl, 2 = restore + UpdateLayerPos, 3 = UpdateLayerData, 4 = BOBs
		// (cookie-cut + esperas), 5 = tiles FG, 6 = contadores+scroll+publicación+punteros,
		// 7 = restore de BOBs, 8 = dibujo de BOBs (cookie-cut), 9 = espera final del
		// Blitter, 10 = periodo del bucle (update + espera de ancla). El efecto ocupa más
		// de un campo (2 campos = 284 204 ciclos): el periodo es el dato de la tasa real.
		static eng::u32 s_prev_t0 = 0;
		eng::debug::DebugPeripheral::counter_value(0, t5 - t0);
		eng::debug::DebugPeripheral::counter_value(1, t1 - t0);
		eng::debug::DebugPeripheral::counter_value(2, t2 - t1);
		eng::debug::DebugPeripheral::counter_value(3, t3 - t2);
		eng::debug::DebugPeripheral::counter_value(4, t5 - t4);
		eng::debug::DebugPeripheral::counter_value(5, t3b - t3);
		eng::debug::DebugPeripheral::counter_value(6, t4 - t3b);
		eng::debug::DebugPeripheral::counter_value(10, t0 - s_prev_t0);
		s_prev_t0 = t0;
		// Rebote de los BOBs entre y=16 e y=224 (`bob_speed`/`bob_y`).
		s16 speed = m_bob_speed;
		s16 y = static_cast<s16>(m_bob_y + speed);
		if (y >= 224) {
			y = 224;
			speed = static_cast<s16>(-speed);
		} else if (y <= 16) {
			y = 16;
			speed = static_cast<s16>(-speed);
		}
		m_bob_y = static_cast<u16>(y);
		m_bob_speed = speed;
		const eng::u32 tw0 = eng::debug::DebugPeripheral::cycle_counter();
		backend.wait_blitter();
		eng::debug::DebugPeripheral::counter_value(9, eng::debug::DebugPeripheral::cycle_counter() - tw0);
		m_updates++;
		eng::debug::mark_frame(g_eng_run_status, m_updates);
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& c) {
		eng::debug::probe_when_ready(g_eng_run_status, m_updates);
	}

private:
	// ------------------------------------------------------------------
	// Utilidades de memoria (CPU)
	// ------------------------------------------------------------------
	static void copy_bytes(u8* dst, const u8* src, u32 bytes) {
		for (u32 i = 0; i < bytes; ++i) { dst[i] = src[i]; }
	}
	static void copy_words(u16* dst, const u16* src, u32 words) {
		for (u32 i = 0; i < words; ++i) { dst[i] = src[i]; }
	}

	// ------------------------------------------------------------------
	// Construcción de la copperlist (`Data/copperlists.asm`, transcripción)
	// ------------------------------------------------------------------
	bool build_clist(eng::Block<eng::CopperTag, eng::MemoryKind::Chip>& block, ListLayout& lay) {
		eng::copper::SchedulerT<false> sched {block};
		// Cabecera: AGA, DMA (la referencia lo activa desde la CPU justo antes de
		// arrancar el Copper; aquí lo hace el propio Copper en la línea 0, mismo estado),
		// ventana, DDF, BPLCON0 (4 planos) y BPLCON2 (PF2>PF1>SPR).
		sched.move(0x1fcu, 0x0000u);
		sched.move(0x096u, 0x83e0u); // DMACON: SETCLR|MASTER|RASTER|COPPER|BLITTER|SPRITE
		sched.move(0x08eu, kDiwstrt);
		sched.move(0x090u, kDiwstop);
		sched.move(0x092u, kDdfstrt);
		sched.move(0x094u, kDdfstop);
		sched.move(0x100u, kBplcon0);
		sched.move(0x104u, 0x0000u);
		// Punteros de sprite (SPR0PT..SPR7PT); se rellenan con `set_spr_ptrs`.
		lay.spr_ptr_word = static_cast<u16>(sched.words_used() + 1u);
		for (u8 c = 0; c < 8u; ++c) {
			sched.move(static_cast<u16>(0x120u + c * 4u), 0u);
			sched.move(static_cast<u16>(0x122u + c * 4u), 0u);
		}
		// Paleta en la línea 42 (justo antes de DIWSTRT=44): 32 colores.
		sched.wait_raw(0x2au, 1u, 0xfffeu);
		sched.emit_palette(kPalette.color, 0u, 32u);
		// Punteros de planos del FG (BPL1..4PT), módulos y BPLCON1 (scroll fino).
		lay.bpl_ptr_word = static_cast<u16>(sched.words_used() + 1u);
		for (u8 p = 0; p < 4u; ++p) {
			sched.move(static_cast<u16>(0x0e0u + p * 4u), 0u);
			sched.move(static_cast<u16>(0x0e2u + p * 4u), 0u);
		}
		sched.move(0x108u, kFgDispMod);
		sched.move(0x10au, kFgDispMod);
		lay.shift_word = static_cast<u16>(sched.words_used() + 1u);
		sched.move(0x102u, 0x0000u);

		// --- Capa de sprites: 212 líneas con VP=$2c..$ff ---
		for (u16 l = 0; l < kLayer1Lines; ++l) {
			sched.wait_raw(static_cast<u16>(kSprFirstLine + l), kWaitHArg, 0xfffeu);
			build_layer_line(sched, l == 0u, true);
		}
		// --- Últimas 12 líneas (VP=$00..$0b → líneas 256..267) ---
		for (u16 l = 0; l < kLayer2Lines; ++l) {
			sched.wait_raw(l, kWaitHArg, 0xfffeu);
			build_layer_line(sched, false, l != (kLayer2Lines - 1u));
		}

		// --- Sub-buffer: línea 268, apaga el DMA de planos, re-engancha a media línea ---
		sched.wait_raw(0x0cu, 1u, 0xfffeu);
		sched.move(0x096u, 0x0100u); // DMACON: limpia BPLEN
		sched.wait_raw(0x0cu, 0x188u, 0xfffeu); // hpos $c5 → palabra $0cc5
		sched.move(0x096u, 0x8100u); // DMACON: activa BPLEN
		sched.move(0x092u, 0x0040u); // DDFSTRT/STOP del sub-buffer (288 px)
		sched.move(0x094u, 0x00c8u);
		sched.move(0x108u, kSbDispMod);
		sched.move(0x10au, kSbDispMod);
		sched.move(0x100u, 0x3200u); // BPLCON0: 3 planos
		sched.move(0x102u, 0x0000u); // BPLCON1: sin shift
		lay.sb_ptr_word = static_cast<u16>(sched.words_used() + 1u);
		for (u8 p = 0; p < 3u; ++p) {
			sched.move(static_cast<u16>(0x0e0u + p * 4u), 0u);
			sched.move(static_cast<u16>(0x0e2u + p * 4u), 0u);
		}
		// Paleta del sub-buffer: 7 colores desde `subpal+2` en **COLOR01..COLOR07**
		// (`color+2` = $DFF182 = COLOR01; la referencia avanza de 2 en 2 hasta $18E).
		for (u8 i = 0; i < 7u; ++i) {
			sched.move(static_cast<u16>(0x182u + i * 2u), kSubPal[1u + i]);
		}
		sched.move(0x104u, 0x0000u);
		sched.end();
		return sched.ok();
	}

	/// Una línea del efecto: 11 columnas Copper + reposición de los 8 canales.
	/// `record` = registrar los índices de las palabras (solo en la línea 0).
	/// `reposition` = emitir la reposición de fin de línea (la última línea no la lleva).
	void build_layer_line(eng::copper::SchedulerT<false>& sched, bool record, bool reposition) {
		// Columnas 1..8 → canales 0..7, posiciones SPRRPOS+0..$38.
		for (u8 c = 0; c < 8u; ++c) {
			if (record) {
				m_lay.pos_word[c] = static_cast<u16>(sched.words_used() + 1u);
				m_lay.datb_word[c] = static_cast<u16>(sched.words_used() + 3u);
			}
			sched.move(static_cast<u16>(0x140u + c * 8u),
				   static_cast<u16>(kSprRPos + c * 8u));
			sched.move(static_cast<u16>(0x146u + c * 8u), 0xaaaau);
			sched.move(static_cast<u16>(0x144u + c * 8u), 0x5555u);
		}
		// Columnas 9..11 → canales 0..2 (reuso), posiciones SPRRPOS+$40..$50.
		for (u8 c = 8u; c < 11u; ++c) {
			if (record) {
				m_lay.pos_word[c] = static_cast<u16>(sched.words_used() + 1u);
				m_lay.datb_word[c] = static_cast<u16>(sched.words_used() + 3u);
			}
			sched.move(static_cast<u16>(0x140u + (c - 8u) * 8u),
				   static_cast<u16>(kSprRPos + c * 8u));
			sched.move(static_cast<u16>(0x146u + (c - 8u) * 8u), 0xaaaau);
			sched.move(static_cast<u16>(0x144u + (c - 8u) * 8u), 0x5555u);
		}
		// Reposición: canales 7..0 a DMAPOS+$38..DMAPOS (orden inverso). Estas palabras
		// son además las «columnas 12..19» que reparte `UpdateLayerPos`.
		if (reposition) {
			for (u8 i = 8u; i-- > 0u;) {
				if (record) {
					// col 12+i (i=0 → ch0 = DMAPOS) va al final del bloque.
					m_lay.pos_word[11u + i] = static_cast<u16>(sched.words_used() + 1u);
				}
				sched.move(static_cast<u16>(0x140u + i * 8u),
					   static_cast<u16>(kDmaRPos + i * 8u));
			}
		}
	}

	// ------------------------------------------------------------------
	// Punteros de la copperlist (`SetSPRPtrs`/`SetFGPtrs`/`SetSBPtrs`)
	// ------------------------------------------------------------------
	/// Escribe los 8 punteros de sprite (`SPRxPT`) de `list` apuntando al juego de estructuras DMA activo (`m_sprshow`). La dirección sale del bloque Chip (`address()`, banco en el tipo): nada que castear.
	void set_spr_ptrs(eng::Words<eng::CopperTag> list) {
		for (u8 c = 0; c < 8u; ++c) {
			const eng::Address<eng::MemoryKind::Chip> a =
				m_spr[m_sprshow].address(static_cast<eng::s32>(static_cast<eng::u32>(c) * kSprStructWords * 2u));
			list[m_lay.spr_ptr_word + c * 4u + 0u] = static_cast<u16>(a.value >> 16u);
			list[m_lay.spr_ptr_word + c * 4u + 2u] = static_cast<u16>(a.value & 0xffffu);
		}
	}

	/// Escribe los 4 punteros de plano (`BPLxPT`) de la copperlist `clist_index` apuntando al buffer FG `buf_index` más `offset` (bytes).
	void set_fg_ptrs(u8 clist_index, u8 buf_index, u16 offset) {
		eng::Words<eng::CopperTag> list = m_clist_words[clist_index];
		const u32 base = static_cast<u32>(m_fg[buf_index].address(static_cast<eng::s32>(offset)).value);
		for (u8 p = 0; p < 4u; ++p) {
			const u32 addr = base + static_cast<u32>(p) * kBufMod;
			list[m_lay.bpl_ptr_word + p * 4u + 0u] = static_cast<u16>(addr >> 16u);
			list[m_lay.bpl_ptr_word + p * 4u + 2u] = static_cast<u16>(addr & 0xffffu);
		}
	}

	/// Escribe los 3 punteros de plano del sub-buffer (`BPLxPT`) en la copperlist 0 (la plantilla).
	void set_sb_ptrs() {
		eng::Words<eng::CopperTag> list = m_clist_words[0];
		const u32 base = static_cast<u32>(m_sb.address(0).value);
		for (u8 p = 0; p < 3u; ++p) {
			const u32 addr = base + static_cast<u32>(p) * kSbMod;
			list[m_lay.sb_ptr_word + p * 4u + 0u] = static_cast<u16>(addr >> 16u);
			list[m_lay.sb_ptr_word + p * 4u + 2u] = static_cast<u16>(addr & 0xffffu);
		}
	}

	// ------------------------------------------------------------------
	// Blits (equivalentes a `GFX/blitter.asm` sobre la API del backend)
	// ------------------------------------------------------------------
	/// `BlitPattern`: rellena `rows` palabras con `value` separadas kLineWords, empezando en la palabra `word_off` de la copperlist `layer`. El offset va en palabras (como en la referencia) y `blit_ptr` lo convierte a bytes al construir la dirección DMA.
	void blit_pattern(eng::amiga::AmigaBackend& backend, eng::Words<eng::CopperTag> layer,
			  eng::u16 word_off, eng::u16 value, eng::u16 rows) {
		backend.blitter_fill_words_strided(
			eng::graphics::blit_ptr(layer, static_cast<eng::s32>(word_off) * 2), value, rows, kLineWords, true);
	}

	/// `BlitClearScreen`: 676 filas × 44 palabras con DMOD=0.
	void blitter_clear_buffer(eng::amiga::AmigaBackend& backend) {
		backend.blitter_blob_run_begin(eng::graphics::BlobOp::Clear, kBufMod, kFgClearRows,
					       0, 0, 0, 0);
		for (u8 i = 0; i < 3u; ++i) {
			backend.blitter_blob_run_one(eng::graphics::BlitPtr {}, eng::graphics::BlitPtr {},
						     eng::graphics::blit_ptr(m_fg[i].view), 0u);
		}
		backend.blitter_blob_run_end();
	}

	// ------------------------------------------------------------------
	// `UpdateSprCtl` (layer.asm): alterna SPRxCTL cada 2 frames
	// ------------------------------------------------------------------
	void update_spr_ctl() {
		const u16 ctl = m_ctl_values[m_ctl_idx >> 1u];
		eng::Words<eng::SpriteTag> set = m_sprset_words[m_sprshow];
		for (u8 c = 0; c < kDmaCols; ++c) {
			set[static_cast<u32>(c) * kSprStructWords + 1u] = ctl;
		}
	}

	// ------------------------------------------------------------------
	// `UpdateLayerPos` (layer.asm): un cuarto de las posiciones por frame
	// ------------------------------------------------------------------
	void update_layer_pos(eng::amiga::AmigaBackend& backend) {
		// Copperlist objetivo: `4 - clist_idx + cpos_idx` (la que se mostrará en el siguiente bloque de 4 frames). Índices en bytes como el original.
		const u8 target = static_cast<u8>(((4u - m_clist_idx) + m_cpos_idx) >> 2u);
		eng::Words<eng::CopperTag> layer = m_clist_words[target];

		switch (m_c4) {
		case 0u: {
			// El original incrementa aquí el offset base de posición y lo usa ya
			// actualizado (`layer.asm:88-98`): +1 (=2 px), con vuelta a 0 al pasar de 7.
			// Es el avance grueso de la capa (0.5 px/frame de media, con el toggle de
			// SPRxCTL cada 2 frames poniendo el píxel impar).
			m_cpos_offset = static_cast<u16>((m_cpos_offset + 1u) & 7u);
			// Columnas 1-4 completas + 3/4 de la 5.
			u16 x = static_cast<u16>(kSprRPos - m_cpos_offset);
			for (u8 c = 0; c < 4u; ++c) {
				blit_pattern(backend, layer, m_lay.pos_word[c], x, kLayerLines);
				x = static_cast<u16>(x + 8u);
			}
			blit_pattern(backend, layer, m_lay.pos_word[4], x, (kLayerLines / 4u) * 3u);
			break;
		}
		case 1u: {
			// 1/4 restante de la 5 + columnas 6-9 + 1/2 de la 10.
			u16 x = static_cast<u16>((kSprRPos + 0x20u) - m_cpos_offset);
			blit_pattern(backend, layer,
				     static_cast<eng::u16>(m_lay.pos_word[4] + (kLayerLines / 4u) * 3u * kLineWords),
				     x, kLayerLines / 4u);
			for (u8 c = 5u; c < 9u; ++c) {
				x = static_cast<u16>(x + 8u);
				blit_pattern(backend, layer, m_lay.pos_word[c], x, kLayerLines);
			}
			x = static_cast<u16>(x + 8u);
			blit_pattern(backend, layer, m_lay.pos_word[9], x, kLayerLines / 2u);
			break;
		}
		case 2u: {
			// 1/2 restante de la 10 + columna 11 + columnas 12-14 (223 líneas) + 1/4
			// de la 15. Desde aquí las posiciones base son DMAPOS.
			u16 x = static_cast<u16>((kSprRPos + 0x48u) - m_cpos_offset);
			blit_pattern(backend, layer,
				     static_cast<eng::u16>(m_lay.pos_word[9] + (kLayerLines / 2u) * kLineWords),
				     x, kLayerLines / 2u);
			x = static_cast<u16>(x + 8u);
			blit_pattern(backend, layer, m_lay.pos_word[10], x, kLayerLines);
			u16 d = static_cast<u16>(kDmaPos - m_cpos_offset);
			for (u8 c = 11u; c < 14u; ++c) {
				blit_pattern(backend, layer, m_lay.pos_word[c], d, kLayerLines - 1u);
				d = static_cast<u16>(d + 8u);
			}
			blit_pattern(backend, layer, m_lay.pos_word[14], d, kLayerLines / 4u);
			break;
		}
		default: {
			// 3/4 restantes de la 15 + columnas 16-19 (223 líneas) + posiciones de las
			// 8 estructuras DMA.
			u16 d = static_cast<u16>((kDmaPos + 0x18u) - m_cpos_offset);
			blit_pattern(backend, layer,
				     static_cast<eng::u16>(m_lay.pos_word[14] + (kLayerLines / 4u) * kLineWords),
				     d, ((kLayerLines / 4u) * 3u) - 1u);
			for (u8 c = 15u; c < kCols; ++c) {
				d = static_cast<u16>(d + 8u);
				blit_pattern(backend, layer, m_lay.pos_word[c], d, kLayerLines - 1u);
			}
			// SPRxPOS de las estructuras DMA (canales 0..7): DMAPOS+8k − cpos_offset
			// (`layer.asm:213-234`), mismo avance grueso que las columnas Copper.
			eng::Words<eng::SpriteTag> set = m_sprset_words[m_sprshow];
			for (u8 c = 0; c < kDmaCols; ++c) {
				set[static_cast<u32>(c) * kSprStructWords] = static_cast<u16>(
					kDmaPos + static_cast<u16>(c * 8u) - m_cpos_offset);
			}
			break;
		}
		}
	}

	// ------------------------------------------------------------------
	// `UpdateLayerData` (layer.asm): DATA repartida en 32 frames
	//
	// `m_bgt_offset` va en **columnas** de tile (la referencia lo lleva en bytes:
	// +2 B = +1 columna; el reset de frame 19 son -22 B = -11 columnas y el de
	// frame 30, -36 B = -18 columnas).
	// ------------------------------------------------------------------
	void update_layer_data(eng::amiga::AmigaBackend& backend) {
		u16 offset = m_bgt_offset;
		const u8 frame = static_cast<u8>(m_c32);
		if (frame < 8u) {
			// Frames 0-7: una columna DMA por frame (canal = frame), 14 tiles de 16x16.
			// Subvista de la estructura del canal a partir de su campo DATA (tras POS+CTL): el destino del blit sale de una vista tipada, sin punteros crudos.
			eng::Words<eng::SpriteTag> dst =
				m_sprset_words[m_sprupdate_idx >> 2u].subspan(static_cast<u32>(frame) * kSprStructWords + 2u);
			const u16* map = kBgTileMap + offset;
			// Una racha de blits (A→D, 32 filas × 1 palabra, sin módulos).
			backend.blitter_blob_run_begin(eng::graphics::BlobOp::Opaque, 1u, 32u, 0, 0, 0, 0);
			for (u16 row = 0; row < 14u; ++row) {
				const u16 tile = map[row * kBgTileRowWords];
				const eng::ByteView<eng::TextureTag> src = m_bg_tiles.subspan(static_cast<u32>(tile) * kBgTileBytes);
				backend.blitter_blob_run_one(eng::graphics::blit_ptr(src), eng::graphics::blit_ptr(src),
							     eng::graphics::blit_ptr(dst.subspan(static_cast<u32>(row) * 32u)), 0u);
			}
			backend.blitter_blob_run_end();
			offset = static_cast<u16>(offset + 1u);
			if (offset >= kBgTileRowWords) { offset = 0u; }
			m_bgt_offset = offset;
			return;
		}
		if (frame < 30u) {
			// Frames 8-18: columnas Copper 1..11 en clist1/clist3.
			// Frames 19-29: las mismas en clist2/clist4 (tras retroceder 11 columnas).
			u8 list = static_cast<u8>(m_cdat_idx >> 2u);
			u8 col = static_cast<u8>(frame - 8u);
			if (frame >= 19u) {
				if (frame == 19u) {
					offset = static_cast<u16>(offset - 11u); // retrocede 11 columnas
					if (static_cast<s16>(offset) < 0) {
						offset = static_cast<u16>(offset + kBgTileRowWords);
					}
				}
				list = static_cast<u8>(list + 1u);
				col = static_cast<u8>(frame - 19u); // 0..10
			}
			// Subvista de la copperlist destino a partir del campo DATB de la columna: los offsets (col, row) van en palabras, como en la referencia.
			eng::Words<eng::CopperTag> dst = m_clist_words[list].subspan(m_lay.datb_word[col]);
			const u16* map = kBgTileMap + offset;
			// Staging lineal: la CPU ensambla la columna (14 tiles) en un scratch Chip y **dos**
			// blits de 1 palabra × 224 líneas escriben DATB y DATA. La secuencia de palabras es
			// la misma que la de los 28 medios-tiles del original (`layer.asm:358-363`):
			// DATB(j) = palabra j+1 del tile y DATA(j) = palabra j, por fila de tile. El coste
			// por job del Blitter domina sobre los datos movidos (VALIDATION.md §4): 2 arranques
			// en vez de 28.
			// DATB(j) = palabra 2j+1 del tile (plano 1 de la línea j) y DATA(j) = palabra 2j
			// (plano 0), con el tile intercalado por línea ([plano0, plano1] por línea).
			u16* staged = m_bg_stage.view.as_words().data();
			for (u16 row = 0; row < 14u; ++row) {
				const u16 tile = map[row * kBgTileRowWords];
				const u16* tw = reinterpret_cast<const u16*>(
					m_bg_tiles.data() + static_cast<u32>(tile) * kBgTileBytes);
				for (u16 j = 0; j < 16u; ++j) {
					staged[row * 16u + j] = tw[2u * j + 1u];
					staged[kLayerLines + row * 16u + j] = tw[2u * j];
				}
			}
			backend.blitter_blob_run_begin(eng::graphics::BlobOp::Opaque, 1u, kLayerLines, 0, 0,
						       kSprColMod, kSprColMod);
			backend.blitter_blob_run_one(eng::graphics::blit_ptr(m_bg_stage.view),
						     eng::graphics::blit_ptr(m_bg_stage.view),
						     eng::graphics::blit_ptr(dst), 0u);
			backend.blitter_blob_run_one(
				eng::graphics::blit_ptr(m_bg_stage.view,
							static_cast<eng::s32>(kLayerLines) * 2),
				eng::graphics::blit_ptr(m_bg_stage.view,
							static_cast<eng::s32>(kLayerLines) * 2),
				eng::graphics::blit_ptr(dst.subspan(2u)), 0u);
			backend.blitter_blob_run_end();
			offset = static_cast<u16>(offset + 1u);
			if (offset >= kBgTileRowWords) { offset = 0u; }
			m_bgt_offset = offset;
			return;
		}
		if (frame == 30u) {
			// Frame 30: retrocede 18 columnas el offset del tilemap.
			offset = static_cast<u16>(offset - 18u);
			if (static_cast<s16>(offset) < 0) {
				offset = static_cast<u16>(offset + kBgTileRowWords);
			}
			m_bgt_offset = offset;
		}
	}

	// ------------------------------------------------------------------
	// `UpdateFGTiles` (tilemap.asm): tiles de 32x32 repartidos en 28 frames
	//
	// `m_fgt_offset` va en **columnas** de tile (la referencia usa el byte offset:
	// +2 B = +1 columna y el tope es la fila completa, 32 columnas).
	// ------------------------------------------------------------------
	void update_fg_tiles(eng::amiga::AmigaBackend& backend) {
		u16 offset = m_fgt_offset;
		const u8 frame = static_cast<u8>(m_c32);
		if (frame >= 28u) {
			if (frame > 28u) { return; }
			offset = static_cast<u16>(offset + 1u);
			if (offset >= kFgTileRowWords) { offset = 0u; }
			m_fgt_offset = offset;
			return;
		}
		if (frame >= 14u) {
			// El segundo cuarto de cada par (frames t+14) ya se pinta junto al primero en el
			// frame t: la segunda mitad del ciclo no tiene trabajo.
			return;
		}
		const u8 f = frame;
		const bool bottom = (f & 1u) != 0u;
		const u8 row = static_cast<u8>(f >> 1u); // fila de tile (0..6)
		offset = static_cast<u16>(offset + row * kFgTileRowWords);
		const u16 tile = kFgTileMap[offset];
		// Los DOS cuartos del tile (16 scanlines = 64 planelíneas) en una racha: fuente contigua
		// (mitad superior o inferior del tile según `bottom`) con `BLTAMOD=0`, destino en la fila
		// de tiles con paso `kBufMod` (dmod = kBufMod-4). Equivale a las dos rachas de 32 que el
		// original reparte entre los frames t y t+14 (`tilemap.asm`): el destino es idéntico (solo
		// entra `fg_offset & 0xfffc`, que no cambia dentro del par) y el segundo cuarto queda
		// escrito 14 frames antes, fuera de la ventana visible (la tira va por delante del scroll).
		const eng::ByteView<eng::TextureTag> src =
			m_fg_tiles.subspan(static_cast<u32>(tile) * kFgTileBytes +
					   (bottom ? kFgHalfBytes : 0u));
		const u32 dest = static_cast<u32>(row) * (kFgMod * 32u) + 40u +
				 (static_cast<u32>(m_fg_offset) & 0xfffcu) +
				 (bottom ? kFgHalfY : 0u);
		backend.blitter_blob_run_begin(eng::graphics::BlobOp::Opaque, 2u, 64u, 0, 0, 0,
					       kBufMod - 4u);
		for (u8 i = 0; i < 3u; ++i) {
			backend.blitter_blob_run_one(eng::graphics::blit_ptr(src), eng::graphics::blit_ptr(src),
						     eng::graphics::blit_ptr(m_fg[i].view, static_cast<eng::s32>(dest)), 0u);
		}
		backend.blitter_blob_run_end();
	}

	// ------------------------------------------------------------------
	// `DrawSubBuffer` (tilemap.asm): 18 tiles de 16x16x3
	// ------------------------------------------------------------------
	void draw_sub_buffer(eng::amiga::AmigaBackend& backend) {
		backend.blitter_blob_run_begin(eng::graphics::BlobOp::Opaque, 1u, 48u, 0, 0, 0,
					       kSbMod - 2u);
		for (u16 i = 0; i < kSbTiles; ++i) {
			const u16 tile = kSbTileMap[i];
			const eng::ByteView<eng::TextureTag> src = m_sb_tiles.subspan(static_cast<u32>(tile) * kSbTileBytes);
			// El tile avanza 1 palabra (16 px) por iteración: la subvista lo refleja sin aritmética de punteros.
			backend.blitter_blob_run_one(eng::graphics::blit_ptr(src), eng::graphics::blit_ptr(src),
						     eng::graphics::blit_ptr(m_sb_words.subspan(i)), 0u);
		}
		backend.blitter_blob_run_end();
	}

	// ------------------------------------------------------------------
	// BOBs: restore desde el 3.er buffer + cookie-cut `$CA` (`BlitBob`)
	// ------------------------------------------------------------------
	// El restore se emite al **principio** del update (tras el ancla, en el hueco de bus del
	// borde), no junto a los BOBs: mide ~77k ciclos si cae en la ventana visible y ~16k en el
	// borde (campaña F4). El original lo lleva al final, pero su cuerpo es mucho más corto y
	// cae igualmente en el borde. El orden restore→BOBs se conserva (los BOBs limpian sobre la
	// copia). Coste: 2 jobs de 15/12 palabras × 128 líneas.
	void restore_bobs(eng::amiga::AmigaBackend& backend) {
		// Selector del original (`btst #0,c32frame_cn_o+1`): siempre falso, buffer visible
		// `fg_buf2` (mismo criterio que el selector de `update()`).
		const bool second = (m_c32 & 0x100u) != 0u;
		// --- Restore: copia el fondo limpio de fg_buf3 al buffer de dibujo ---
		// Los BOBs de una fila están a 48 px = 3 palabras y comparten alineación, así que
		// sus celdas de 3 palabras son contiguas: la unión de la fila de 5 BOBs son 15
		// palabras y la de 4 son 12. Dos copias equivalen a las 9 del original
		// (`SPR_Layer.asm:404-411`), que escriben exactamente las mismas palabras.
		RestoreEntry* entries = m_restore[second ? 0 : 1];
		const eng::u32 tr0 = eng::debug::DebugPeripheral::cycle_counter();
		if (entries[0].dst.addr.valid()) {
			backend.blitter_blob_run_begin(eng::graphics::BlobOp::Opaque, 15u, 128u, 14, 14,
						       14, 14);
			backend.blitter_blob_run_one(entries[0].src, entries[0].src, entries[0].dst, 0u);
			backend.blitter_blob_run_end();
			backend.blitter_blob_run_begin(eng::graphics::BlobOp::Opaque, 12u, 128u, 20, 20,
						       20, 20);
			backend.blitter_blob_run_one(entries[1].src, entries[1].src, entries[1].dst, 0u);
			backend.blitter_blob_run_end();
		}
		eng::debug::DebugPeripheral::counter_value(7, eng::debug::DebugPeripheral::cycle_counter() - tr0);
	}

	void draw_bob_cells(eng::amiga::AmigaBackend& backend) {
		const bool second = (m_c32 & 0x100u) != 0u;
		// --- Dibujo: 9 BOBs a X = 40 + (c32 & 15) + 24*i ---
		const u8 buf = second ? 0u : 1u;
		const u32 scroll = m_fg_offset;
		u16 x = static_cast<u16>((m_c32 & 0x0fu) + m_bob_x);
		RestoreEntry* out = m_restore[second ? 0 : 1];
		const eng::u32 tr1 = eng::debug::DebugPeripheral::cycle_counter();
		// Entradas de restore del próximo frame: la unión de las celdas de cada fila (fila par: BOBs 0,2,4,6,8 a X = x y 224-bob_y; impar: 1,3,5,7 a X = x+24 y bob_y). Las direcciones se construyen desde las vistas del buffer limpio (m_fg[2]) y del buffer de dibujo, con el offset ya en bytes.
		{
			const u32 off_even = static_cast<u32>(224u - m_bob_y) * kFgMod + (x >> 3u);
			const u32 off_odd = static_cast<u32>(m_bob_y) * kFgMod + ((x + 24u) >> 3u);
			out[0].src = eng::graphics::blit_ptr(m_fg[2].view, static_cast<eng::s32>(scroll + off_even));
			out[0].dst = eng::graphics::blit_ptr(m_fg[buf].view, static_cast<eng::s32>(scroll + off_even));
			out[1].src = eng::graphics::blit_ptr(m_fg[2].view, static_cast<eng::s32>(scroll + off_odd));
			out[1].dst = eng::graphics::blit_ptr(m_fg[buf].view, static_cast<eng::s32>(scroll + off_odd));
		}
		backend.blitter_blob_run_begin(eng::graphics::BlobOp::CookieCut, 3u, 128u,
					       static_cast<s16>(0xfffe), static_cast<s16>(0xfffe),
					       38, 38);
		// La ventana de máscaras de la referencia: la última palabra del blit se descarta (`bltalwm=0`) para que el desplazamiento no escriba la palabra 3.
		backend.blitter_blob_run_masks(0xffffu, 0x0000u);
		for (u8 i = 0; i < kBobCount; ++i) {
			const u16 y = ((i & 1u) == 0u) ? static_cast<u16>(224u - m_bob_y) : m_bob_y;
			// y*176 + (x>>3): sin multiplicación (176 = 16+32+128).
			const u32 off = static_cast<u32>(y) * kFgMod + (x >> 3u);
			const eng::graphics::BlitPtr dst =
				eng::graphics::blit_ptr(m_fg[buf].view, static_cast<eng::s32>(scroll + off));
			backend.blitter_blob_run_one(eng::graphics::blit_ptr(m_masks), eng::graphics::blit_ptr(m_bobs), dst,
						     static_cast<u8>(x & 0x0fu));
			x = static_cast<u16>(x + 24u);
		}
		backend.blitter_blob_run_end();
		eng::debug::DebugPeripheral::counter_value(8, eng::debug::DebugPeripheral::cycle_counter() - tr1);
	}

	// ------------------------------------------------------------------
	// Contadores y scroll (bucle principal de `SPR_Layer.asm`)
	// ------------------------------------------------------------------
	void update_counters() {
		// Contador de 2 frames → índice de SPRxCTL (0/2).
		u16 d1 = static_cast<u16>(m_c2 + 1u);
		if (d1 >= 2u) {
			d1 = 0u;
			u16 d2 = static_cast<u16>(m_ctl_idx + 2u);
			if (d2 >= 4u) { d2 = 0u; }
			m_ctl_idx = d2;
		}
		m_c2 = d1;
		// Contador de 4 frames → índice de copperlist (0/4).
		d1 = static_cast<u16>(m_c4 + 1u);
		if (d1 >= 4u) {
			d1 = 0u;
			u16 d2 = static_cast<u16>(m_clist_idx + 4u);
			if (d2 >= 8u) { d2 = 0u; }
			m_clist_idx = d2;
		}
		m_c4 = d1;
		// Contador de 32 frames: en el 28 avanza el índice de posiciones; al cerrar
		// el ciclo avanzan los índices de lista mostrada, DATA, sprites y juego.
		d1 = static_cast<u16>(m_c32 + 1u);
		if (d1 == 28u) {
			u16 d2 = static_cast<u16>(m_cpos_idx + 8u);
			if (d2 >= 16u) { d2 = 0u; }
			m_cpos_idx = d2;
		} else if (d1 >= 32u) {
			d1 = 0u;
			u16 d2 = static_cast<u16>(m_cshow_idx + 8u);
			if (d2 >= 16u) { d2 = 0u; }
			m_cshow_idx = d2;
			d2 = static_cast<u16>(m_cdat_idx + 8u);
			if (d2 >= 16u) { d2 = 0u; }
			m_cdat_idx = d2;
			d2 = static_cast<u16>(m_sprupdate_idx + 4u);
			if (d2 >= 8u) { d2 = 0u; }
			m_sprupdate_idx = d2;
			d2 = static_cast<u16>(m_sprshow_idx + 4u);
			if (d2 >= 8u) { d2 = 0u; }
			m_sprshow_idx = d2;
			m_sprshow = static_cast<u8>(m_sprshow_idx >> 2u);
		}
		m_c32 = d1;
	}

	void update_scroll() {
		// BPLCON1 = v | v<<4 con v = 15 - (c32 & 15): el píxel fino del scroll.
		const u16 low = static_cast<u16>(m_c32 & 0x0fu);
		const u16 v = static_cast<u16>(0x0fu - low);
		m_fg_shift = static_cast<u16>(v | (v << 4u));
		// El puntero de planos avanza 2 bytes cada 16 frames (16 px).
		if (low != 0u) { return; }
		const u16 off = static_cast<u16>(m_fg_offset + kScrollPan);
		if (off < kFgScrollMax) {
			m_fg_offset = off;
		}
	}

	/// Estado inicial del bucle principal (`SPR_Layer.asm` tras el título).
	void start_main_loop() {
		m_c2 = 1u;
		m_c4 = 0u;
		m_c32 = 0u;
		m_ctl_idx = 0u;
		m_clist_idx = 0u;
		m_cpos_idx = 0u;
		m_cshow_idx = 0u;
		m_cdat_idx = 8u;
		m_sprupdate_idx = 4u;
		m_sprshow_idx = 0u;
		m_sprshow = 0u;
		m_cpos_offset = 0u;
		m_fg_shift = 0x00ffu;
		m_fg_offset = 4u;
		m_bgt_offset = 0u;
		m_fgt_offset = 0u;
		for (u8 s = 0; s < 2u; ++s) {
			for (u8 i = 0; i < 2u; ++i) {
				m_restore[s][i].src = {};
				m_restore[s][i].dst = {};
			}
		}
		m_bob_speed = -2;
		m_bob_x = 40u;
		m_bob_y = 112u;
	}

	// ------------------------------------------------------------------
	// Fuente 8x8 (PlotCharCPU/PlotTextMultiCPU de `GFX/font.asm`)
	// ------------------------------------------------------------------
	/// Pinta un carácter de 8x8 en un bitmap **intercalado por línea**:
	/// `row_stride` = bytes por scanline, `plane_stride` = bytes entre planos.
	void plot_char(u8* base, u16 x_cell, u16 y_cell, u8 depth, u8 color, u16 row_stride,
		       u16 plane_stride, char ch) {
		const u8 idx = static_cast<u8>(ch - 32);
		u8* d = base + static_cast<u32>(y_cell) * 8u * row_stride + x_cell;
		const u8* glyph = m_font + static_cast<u32>(idx) * 8u;
		for (u8 p = 0; p < depth; ++p) {
			if ((color & (1u << p)) != 0u) {
				for (u8 r = 0; r < 8u; ++r) {
					d[static_cast<u32>(r) * row_stride] = glyph[r];
				}
			} else {
				for (u8 r = 0; r < 8u; ++r) {
					d[static_cast<u32>(r) * row_stride] = 0u;
				}
			}
			d += plane_stride;
		}
	}

	void plot_text_multi(u8* base, u16 row_stride, u16 plane_stride, u8 depth,
			     const TextLine* lines, usize count) {
		for (usize i = 0; i < count; ++i) {
			const TextLine& l = lines[i];
			u16 x = l.x;
			for (const char* p = l.text; *p != '\0'; ++p) {
				if (static_cast<u8>(*p) < 32u) { continue; }
				plot_char(base, x, l.y, depth, l.color, row_stride, plane_stride, *p);
				x = static_cast<u16>(x + 1u);
			}
		}
	}

	// ------------------------------------------------------------------
	// Estado (espejo de las variables de `SPR_Layer.asm`)
	// ------------------------------------------------------------------
	/// Entrada de restore de una fila de BOBs: origen (el buffer limpio m_fg[2]) y destino (el buffer de dibujo) como **direcciones DMA tipadas** (BlitPtr), no punteros crudos.
	struct RestoreEntry {
		eng::graphics::BlitPtr src {}; ///< Origen de la copia de restore (fondo limpio, sin BOBs).
		eng::graphics::BlitPtr dst {}; ///< Destino de la copia (el buffer FG que se está dibujando).
	};

	// Palabras de la copperlist: 18982 de la referencia + el MOVE de DMACON de la
	// cabecera (la referencia lo hace desde la CPU) + margen de seguridad.
	static constexpr u32 kClistWords = 18990u;
	static constexpr u32 kClistBytes = kClistWords * 2u;

	// Bloques DMA: todos se reservan de `MemBank<Chip>`, así que llevan el banco **en el tipo** y sus direcciones (`address()`/`mem_view()`) son DMA-seguras sin comprobaciones ni casts.
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_fg[3] {};      ///< Los 3 buffers del playfield (texto + BOBs); el 3.º es la copia limpia para el restore.
	eng::Block<eng::CopperTag, eng::MemoryKind::Chip> m_clist[4] {};  ///< Las 4 copperlists (dos parejas, doble buffer de 32 frames).
	eng::Block<eng::CopperTag, eng::MemoryKind::Chip> m_minimal {};   ///< Lista que aparca el Copper durante el init (WAIT que nunca dispara).
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_spr[2] {};    ///< Los 2 juegos de estructuras DMA de sprite (8 canales × 516 palabras).
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_sb {};         ///< Sub-buffer (barra inferior, 3 planos).
	eng::Block<eng::TextureTag, eng::MemoryKind::Chip> m_assets {};   ///< Assets copiados a Chip: tiles BG/FG, tiles del sub-buffer, BOBs y máscaras.
	eng::Block<eng::TextureTag, eng::MemoryKind::Chip> m_bg_stage {}; ///< Scratch lineal de una columna de fondo: 2 × 224 palabras (DATB, DATA) para los 2 blits del staging.
	eng::Words<eng::CopperTag> m_clist_words[4] {};                   ///< Ventanas de trabajo (words) sobre las copperlists: se parchean por CPU y se instalan/entregan al Blitter.
	eng::Words<eng::SpriteTag> m_sprset_words[2] {};                  ///< Ventanas de trabajo sobre los 2 juegos de estructuras DMA.
	eng::Words<eng::PlaneTag> m_sb_words {};                          ///< Ventana de trabajo sobre el sub-buffer (words de sus 3 planos).
	eng::ByteView<eng::TextureTag> m_bg_tiles {};                     ///< Vista de los tiles de fondo dentro del bloque de assets (Chip).
	eng::ByteView<eng::TextureTag> m_fg_tiles {};                     ///< Vista de los tiles de foreground (glifos) dentro del bloque de assets.
	eng::ByteView<eng::TextureTag> m_sb_tiles {};                     ///< Vista de los tiles del sub-buffer dentro del bloque de assets.
	eng::ByteView<eng::TextureTag> m_bobs {};                         ///< Vista de la hoja de BOBs (4 planos intercalados) dentro del bloque de assets.
	eng::ByteView<eng::TextureTag> m_masks {};                        ///< Vista de las máscaras de cookie-cut de los BOBs dentro del bloque de assets.
	const u8* m_font = nullptr;                                       ///< Glifos 8x8 del binario: **solo CPU** (se dibujan con `plot_char`), no pasa por DMA, por eso sigue crudo.
	ListLayout m_lay {};

	u16 m_c2 = 0u;
	u16 m_c4 = 0u;
	u16 m_c32 = 0u;
	// Contador de *updates* (no de ticks de VBlank): el efecto puede ocupar más de un
	// campo, y el frame_index del engine lo avanza la IRQ a 50 Hz. La telemetría y los
	// pasos de secuencia deben contar actualizaciones reales del efecto.
	u32 m_updates = 0u;
	u16 m_ctl_idx = 0u;
	u16 m_clist_idx = 0u;
	u16 m_cpos_idx = 0u;
	u16 m_cshow_idx = 0u;
	u16 m_cdat_idx = 0u;
	u16 m_sprupdate_idx = 0u;
	u16 m_sprshow_idx = 0u;
	u8 m_sprshow = 0u;
	u16 m_cpos_offset = 0u;
	u16 m_fg_shift = 0u;
	u16 m_fg_offset = 0u;
	u16 m_bgt_offset = 0u;
	u16 m_fgt_offset = 0u;
	const u16 m_ctl_values[2] {0x0c03u, 0x0c02u};
	s16 m_bob_speed = 0;
	u16 m_bob_x = 0u;
	u16 m_bob_y = 0u;
	// [doble buffer][fila de BOBs: 0 = pares (y=224-bob_y), 1 = impares (y=bob_y)]
	RestoreEntry m_restore[2][2] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	SprLayerDemo game {};
	eng::Engine engine {backend, game};
	// Modelo de mensajes: el latido va por la IRQ de VBlank y el bucle principal duerme
	// en `Wait()` cuando no hay nada que procesar.
	// Esta demo no usa tareas de fondo (`BackgroundQueue` vacía): desactivar el servicio
	// de blit evita que `wait_blitter` llame al servicio en **cada** iteración del sondeo
	// de BBUSY (`amiga_internal.hpp:210-221`), ~130k ciclos/update de coste inútil. La
	// espera queda como el sondeo «nasty» puro del original (`BlitWait`,
	// `spr_layer/Sprite_Layer/GFX/blitter.i:26-31`) y el update cabe en 2 campos exactos.
	engine.set_blit_service_enabled(false);
	(void)eng::os::init(engine, 0u);
	engine.run_frames(0xffff);

	return 0;
}
