#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/field/tile_demo.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/platform/input_poll.hpp>
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

// Telemetría por frame del coste de render (blits, words, copper): se lee por el
// canal lateral de WinUAE-DBG (`mem <addr> <len>`) para supervisar la homogeneidad
// de la carga. Ver docs/debugging/DEBUG-WINUAE-V2-GUIDE.md y run_status.hpp.
extern "C" {
__attribute__((used)) volatile eng::debug::FrameTelemetry g_eng_frame_telemetry {};
}

// Gancho de conmutación de técnica (1..9). Lo alimenta el input (CIAA, §15
// input_poll cuando exista) o el auto-ciclo; el update lo consume y lo pone a 0.
extern "C" {
__attribute__((used)) volatile eng::u8 g_tech_new = 0;
}

// Tecla sintética inyectada por el host (automatización por memoria de WinUAE:
// scancode Amiga, '1'=0x02..; 0xff = sin tecla). Ámbito global + enlace C (sin
// mangle) para que el runner resuelva su dirección y le escriba el `poke`.
extern "C" {
__attribute__((used)) volatile eng::u8 g_automation_keycode = 0xffu;
}

namespace {

namespace field = eng::field;
namespace demo = eng::field::demo;

// -----------------------------------------------------------------------------
// Demo 107 — xlimited_corkscrew (corkscrew / XYLimited, Georg Steger)
// -----------------------------------------------------------------------------
//
// QUÉ HACE
// --------
// Scroll 8-way (horizontal, vertical, HV y diagonal) con el algoritmo corkscrew
// de ScrollingTricks (Scroller_XYLimited). Un único bitmap interleaved de
// BITMAPWIDTH×(viewport_h + EXTRAHEIGHT + extra) con el display envolviendo en
// `display_height = viewport_h + 2*tile_height` (banda de staging de 2 bloques)
// y un split de Copper para el wrap vertical. Cada píxel es 1-2 blits de
// BLOCKPLANELINES = tile_h*planes planelíneas; la fila/columna entrante se
// pre-pinta en la banda de staging (`block_videoposy`, envuelto en
// `display_height`) y la guarda de 1 word (`saveword`) evita el hueco de 2 bytes
// al invertir la dirección. Ver docs/architecture/AMIGA_8WAY_SCROLLING.md §13.
//
// Ciclo de fases (1000 frames = ~20 s cada una, 1 px/frame):
//   0: horizontal derecha   1: vertical abajo
//   2: HV alternando        3: diagonal Lissajous (sin(x), cos(0.7x))
//
// -----------------------------------------------------------------------------
// CÓMO COMPILAR (tools/build/build-demo.sh)
// -----------------------------------------------------------------------------
// El toolchain se resuelve en este orden: $AMIGA_BIN_PATH, extensión
// bartmanabyss.amiga-debug-* (Windows) o m68k-amiga-elf-* en PATH.
//
//   # Configuración por defecto (320×256, tile 16, 4 planos, fetch normal):
//   bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean
//
//   # Tiles de 32 px (misma altura, BITMAPWIDTH=384, 24 bloques/fila):
//   EXTRA_DEFINES="-DK_TILE_WIDTH=32" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean
//
//   # Fetch ancho 32 px (BPL32, DDF $28/$C8, offset 16, scroll 32):
//   EXTRA_DEFINES="-DK_FETCH_MODE=1" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean
//
//   # Fetch ancho 32 px + tiles 32 px:
//   EXTRA_DEFINES="-DK_TILE_WIDTH=32 -DK_FETCH_MODE=1" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean
//
//   # Fetch ancho 64 px (BPL32+BPAGEM, DDF $18/$B8, offset 48, scroll 64):
//   EXTRA_DEFINES="-DK_FETCH_MODE=3" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean
//
//   # Viewport alternativo 288×224 (18×14 tiles por pantalla):
//   EXTRA_DEFINES="-DK_VIEWPORT_W=288 -DK_VIEWPORT_H=224" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean
//
// Parámetros de compilación (EXTRA_DEFINES="-D..."):
//   K_EFFECT     (0|1..8)  0=todas en ciclo · 1=H der · 2=H izq · 3=V abajo · 4=V arriba
//                          · 5=HV der/abj · 6=HV izq/arr · 7=diagonal · 8=diagonal inverso
//   K_START_PHASE (0..7)   fase inicial del ciclo K_EFFECT=0
//   K_PHASE_FRAMES (1000)  frames por fase del ciclo (~20 s a 50 fps)
//   K_PRE_SCROLL  (1024)   px de pre-scroll en init para fases reversas/diagonales
//   K_DUAL        (0|1)     DPF 3+3: dos XLimitedPlayfield + XlimitedDualComposer (PF1 planos 1,3,5 / PF2 2,4,6)
//   K_PARALLAX    (0|1)     con K_DUAL=1, PF2 scrollea a media velocidad en X
//   K_LINEAR      (0|1)     display lineal sin split (espejo del bucle); elimina el
//                           artefacto del split en raster 256..296 a costa de 2× blits
//   K_TILE_WIDTH (16|32)  ancho de tile (múltiplo de 16; con fetch ancho da BITMAPWIDTH=384,
//                         con fetch normal 352; bitmap_width auto = viewport + EXTRAWIDTH)
//   K_TILE_SIZE  (16|32)  alto de tile (K_TILE_H es alias)
//   K_PLANES     (3|4|5|6) profundidad (8/16/32/64 colores; 6 = EHB/DPF 3+3)
//   K_FETCH_MODE (0|1|2|3) modo de fetch: 0=16px $30 mod 2 · 1/2=32px $28 mod 4 · 3=64px $18 mod 8
//   K_VIEWPORT_W/H (320/224 | 288/208 | 256 con K_LINEAR=1)  ventana visible
//   K_SCREENS_X/Y (16|8...)  pantallas virtuales → mapa = screens*viewport/tile
//   K_STEP       (1|2..16)   salto máx. px/frame por eje (sub-pasos atómicos de 1 px,
//                            paint-then-advance: NUNCA a medio pintar). 2 = fluido; 16 =
//                            columna nueva completa. Coste = nº de jobs de Blitter ∝ salto.
//   K_STEP_BG    (0|2..)     salto de PF2 (BG) en dual_patterns (0 = igual a K_STEP)
//   K_PATTERNS   (0|1)       DPF con cada PF en un patrón distinto (BG diagonal / FG Lissajous)
//   K_LISSAJOUS_OFF (0..63)  offset de fase inicial del Lissajous del FG
//
// -----------------------------------------------------------------------------
// CÓMO LANZAR EN WINUAE (tools/run/run-demo.sh -> dist/tools/run/run-demo.js)
// -----------------------------------------------------------------------------
// El runner arranca WinUAE-DBG, espera READY por el canal lateral (127.0.0.1:2346,
// estado g_eng_run_status) y captura screenshot; sin --keep-running apaga WinUAE
// al terminar. Requiere la demo compilada y Node.js + Git Bash.
//
//   # Captura básica (screenshot en out/run/107_xlimited_corkscrew/screenshot.png):
//   bash ./tools/run/run-demo.sh demos/107_xlimited_corkscrew
//
//   # Capturar N frames de secuencia cada M ms (análisis de scroll):
//   bash ./tools/run/run-demo.sh demos/107_xlimited_corkscrew --sequence-frames 100 --sequence-interval-ms 20
//
//   # Asentamiento previo (ms reales tras READY) y warp (throughput, no suavidad):
//   bash ./tools/run/run-demo.sh demos/107_xlimited_corkscrew --settle-ms 500 --warp
//
//   # Dejar WinUAE corriendo al terminar:
//   bash ./tools/run/run-demo.sh demos/107_xlimited_corkscrew --keep-running
//
// -----------------------------------------------------------------------------
// CÓMO VERIFICAR
// -----------------------------------------------------------------------------
//   # Secuencia completa de la demo (H derecha + saveword + columna plane-shifted):
//   bash ./demos/107_xlimited_corkscrew/analyze-sequence.sh          # tiempo real
//   bash ./demos/107_xlimited_corkscrew/analyze-sequence.sh --warp   # rápido
//
//   # Análisis de artefactos de la captura:
//   bash ./tools/analyze/analyze-demo.sh demos/107_xlimited_corkscrew
//
//   # Modelo host del algoritmo (geometría XLimited + corkscrew):
//   node tools/analyze/verify-xlimited.mjs
//
//   # Port vs original (bloque a bloque contra Scroller_XYLimited/main.c):
//   node tools/analyze/verify-corkscrew.mjs
//
//   # Regresión automática (build -> run -> analyze-sequence):
//   bash ./tools/test-regression.sh --demo demos/107_xlimited_corkscrew --warp
//
// -----------------------------------------------------------------------------
// Configuración de la demo 107 — corkscrew (single playfield) parametrizada
// -----------------------------------------------------------------------------
//
// El flujo es el mismo que en Steger:
//
//   CPU: decide mapposx/videoposx, mapposy/videoposy y DDF/BPLCON1
//        |                |
//        +--> Blitter: 1-2 blits de BLOCKPLANELINES = tile_h*planes líneas por píxel de scroll
//             (ej. 16*3=48, 16*4=64, 16*5=80, 16*6=96 según K_PLANES 3..6)
//        +--> Copper: BPLxPT + BPLCON1/BPLMOD + split vertical en display_height
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
#define K_VIEWPORT_H 224 // canónico juegos: 224 filas visibles + 32 de borde/HUD debajo
#endif
#ifndef K_SCREENS_X
#define K_SCREENS_X 16
#endif
#ifndef K_SCREENS_Y
#define K_SCREENS_Y 16
#endif
// Selector de efecto/dirección para verificar la composición de tiles en ambos sentidos.
//   K_EFFECT   0 = todas las fases en orden (ciclo, ver K_START_PHASE/K_PHASE_FRAMES)
//              1 = horizontal derecha      2 = horizontal izquierda (pre-scroll derecho)
//              3 = vertical abajo          4 = vertical arriba   (pre-scroll abajo)
//              5 = HV alternando derecha/abajo     6 = HV alternando izquierda/arriba
//              7 = diagonal Lissajous              8 = diagonal Lissajous inverso
//   K_START_PHASE   fase inicial del ciclo "todas" (0..7, defecto 0)
//   K_PHASE_FRAMES  frames por fase en el ciclo (defecto 300 ≈ 6 s a 50 fps:
//                   las 8 fases del 8-way se recorren en ~48 s; muestrario)
//   K_PRE_SCROLL    px de pre-scroll hacia delante para que las fases reversas tengan recorrido
#ifndef K_EFFECT
#define K_EFFECT 0
#endif
#ifndef K_START_PHASE
#define K_START_PHASE 0
#endif
#ifndef K_PHASE_FRAMES
#define K_PHASE_FRAMES 300
#endif
#ifndef K_PRE_SCROLL
#define K_PRE_SCROLL 1024
#endif
// DPF 3+3: dos XLimitedPlayfield (PF1 planos 1,3,5 / PF2 2,4,6), cada uno con su
// bitmap interleaved de K_PLANES=3, unidos por XlimitedDualComposer. Ambos
// scrollean en la misma dirección (K_EFFECT) y comparten videoposy (mismo split).
// MUESTRARIO (defecto DUAL=1): PF1 es el FG con la mitad de tiles transparentes
// (checkerboard sobre tile 0 vacío: PF1 deja ver PF2), PF2 es el fondo opaco con
// su propio mapa. La regresión fuerza K_DUAL=0 (línea base single pixel-exacta).
#ifndef K_DUAL
#define K_DUAL 1
#endif
#ifndef K_PARALLAX
#define K_PARALLAX 0 // si 1, PF2 (fondo) scrollea a media velocidad en X
#endif
// Display lineal sin split (espejo del bucle): elimina la banda del split en
// raster 256..296 (comparador de 8 bits) a costa de 2x blits (dibujo + espejo).
// Para el viewport canónico de juego (224) el split cae casi siempre en líneas
// esperables, así que por defecto se usa el split (1x blits). Usa K_LINEAR=1
// solo para viewports altos (256) o si quieres eliminar el residuo del split.
#ifndef K_LINEAR
#define K_LINEAR 0
#endif
#ifndef K_HUD
#define K_HUD 1 // 0 = sin HUD (ni franja inferior ni marcas de ejemplo)
#endif
#ifndef K_BOB
#define K_BOB 1 // 0 = sin el BOB enmascarado de mundo (aislar artefactos)
#endif
// DPF heterogéneo: el FG es un CanvasPlayfield (lienzo plano estático) en vez de
// un segundo XLimited. Requiere K_DUAL=1 (BG corkscrew en PF1, FG lienzo en PF2).
#ifndef K_CANVAS_FG
#define K_CANVAS_FG 0
#endif
// Sprites hardware a nivel de escena (delante de los playfields, paleta 16-31).
#ifndef K_SPRITE
#define K_SPRITE 1 // muestrario: sprite (diamante) delante de los playfields
#endif
// Salto configurable: px/frame MÁXIMOS por eje (el scroll se ejecuta como
// sub-pasos atómicos de 1 px, paint-then-advance → nunca a medio pintar).
//   K_STEP       salto máx. de ambos playfields (defecto 8 en el muestrario DPF)
//   K_STEP_BG    salto máx. del BG (PF2) en dual_patterns (0 = igual a K_STEP)
//   K_PATTERNS   DPF con cada playfield en un patrón distinto:
//                BG diagonal (acelera→8px→frena→revierte), FG Lissajous
#ifndef K_STEP
#define K_STEP 2 // default fluido ~50fps+; subir = más Blitter/frame (el pico 8 exige fusión por tile)
#endif
#ifndef K_STEP_BG
#define K_STEP_BG 2
#endif
#ifndef K_PATTERNS
#define K_PATTERNS 1
#endif
#ifndef K_LISSAJOUS_OFF
#define K_LISSAJOUS_OFF 16 // offset de fase inicial del Lissajous del FG (0..63)
#endif
// K_COHERENT: mapa de nivel con REGIONES uniformes (hierba/muro/agua/camino) en
// lugar de tiles aleatorios. Explota el [skip-on-equal] del engine: cuando la
// columna/fila entrante repite el tile ya pintado en la banda, se OMITEN los
// jobs del Blitter (los niveles reales son coherentes → muchos menos jobs).
#ifndef K_COHERENT
#define K_COHERENT 0
#endif
// Técnicas conmutables en runtime (todas compiladas). K_START_TECH = técnica
// inicial (0..N-1); K_AUTOCYCLE>0 avanza de técnica cada tantos frames (para
// verificar el ciclo de vida sin teclado); el teclado real (CIAA, §15 input)
// alimentará `g_tech_new` (1..N) para conmutar.
#ifndef K_START_TECH
#define K_START_TECH 0
#endif
#ifndef K_AUTOCYCLE
#define K_AUTOCYCLE 0 // frames por técnica en modo automático (0 = off)
#endif
// Franja HUD inferior: un playfield SEPARADO (CanvasPlayfield) de K_HUD_HEIGHT
// filas con K_HUD_PLANES bitplanes y paleta propia, en una zona de Copper bajo
// el viewport principal. El WAIT de la zona debe caer en raster <= 255.
#ifndef K_HUD_HEIGHT
#define K_HUD_HEIGHT 32 // total 224 -> main 192 + HUD 32
#endif
#ifndef K_HUD_PLANES
#define K_HUD_PLANES 4
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
constexpr eng::u8 kEffectMode = static_cast<eng::u8>(K_EFFECT);        // 0=ciclo, 1..8=efecto único
constexpr eng::u8 kStartPhase = static_cast<eng::u8>(K_START_PHASE);   // fase inicial del ciclo (0..7)
constexpr eng::u32 kPhaseFrames = static_cast<eng::u32>(K_PHASE_FRAMES);
constexpr eng::s32 kPreScroll = static_cast<eng::s32>(K_PRE_SCROLL);
constexpr eng::u8 kPhaseCount = 8;
constexpr bool kDual = K_DUAL != 0;                                     // DPF 3+3 (dos playfields)
constexpr bool kParallax = K_PARALLAX != 0;                             // PF2 scrollea a media X
constexpr bool kDualPatterns = K_PATTERNS != 0;                         // DPF: patrones por PF
constexpr eng::u8 kStepMax = static_cast<eng::u8>(K_STEP);              // salto máx. (px/frame)
constexpr eng::u8 kStepBg = static_cast<eng::u8>(K_STEP_BG);            // salto BG (0 = igual)
constexpr eng::u8 kLissajousOffset = static_cast<eng::u8>(K_LISSAJOUS_OFF);
constexpr bool kLinear = K_LINEAR != 0;                                 // display lineal sin split
constexpr eng::u8 kEffectivePlanes = kDual ? 3u : kPlanes;              // 3 por playfield en DPF
// 16×16 pantallas virtuales → mapa en tiles derivado de viewport/tile
constexpr eng::u16 kMapTilesX = static_cast<eng::u16>(K_SCREENS_X * (K_VIEWPORT_W / K_TILE_W));
constexpr eng::u16 kMapTilesY = static_cast<eng::u16>(K_SCREENS_Y * (K_VIEWPORT_H / K_TILE_H));
constexpr eng::u8 kTileCount = 64;

// Geometría como constantes a priori (NTTP) para el `ScrollEngine`: los
// denominadores calientes (tile 16/16, display_height, display_planelines,
// planes) entran compile-time → `fast_div` (shifts/máscaras/mult-mágica) y cero
// `__udivsi3` en el scroll por frame. `main_h` resta la franja HUD (solo si K_HUD).
constexpr eng::u32 kMainH = (K_HUD != 0)
    ? static_cast<eng::u32>(K_VIEWPORT_H) - static_cast<eng::u32>(K_HUD_HEIGHT)
    : static_cast<eng::u32>(K_VIEWPORT_H);
constexpr eng::u32 kMainDisplayH = kMainH + 2u * static_cast<eng::u32>(K_TILE_SIZE);
constexpr field::ScrollConsts kScrollConsts {
    /*tile_width=*/        static_cast<eng::u32>(K_TILE_WIDTH),
    /*tile_height=*/       static_cast<eng::u32>(K_TILE_SIZE),
    /*display_height=*/    kMainDisplayH,
    /*display_planelines=*/kMainDisplayH * kEffectivePlanes,
    /*planes=*/            kEffectivePlanes,
};

// --- Técnicas conmutables (muestrario DPF): todas compiladas; cada una
//     PARAR/INICIAR su scroll (ciclo de vida) al conmutar con 1..9 (g_tech_new).
enum PatternMode : eng::u8 { P8Way = 0, PDualPatterns = 1 };
struct Technique {
    const char* name;
    PatternMode pattern; // 0 = auto 8-way (ambos igual) · 1 = dos patrones
    eng::u8 step;        // px/frame del 8-way
    eng::u8 stepBg;      // px/frame del BG (diagonal) en patrones
    eng::u8 stepFg;      // px/frame del FG (Lissajous) en patrones
};
static constexpr Technique kTechniques[] {
    { "DPF · patrones (BG diag + FG Lissajous) 2px", PDualPatterns, 0, 2, 2 },
    { "DPF · 8-way (ambos mismo paso) 2px",           P8Way,         2, 0, 0 },
    { "DPF · patrones 4px",                           PDualPatterns, 0, 4, 4 },
    { "DPF · 8-way 4px",                              P8Way,         4, 0, 0 },
    { "DPF · patrones 1px",                           PDualPatterns, 0, 1, 1 },
    { "DPF · 8-way 1px",                              P8Way,         1, 0, 0 },
};
static constexpr eng::u8 kTechCount = static_cast<eng::u8>(
    static_cast<eng::u32>(sizeof(kTechniques) / sizeof(kTechniques[0]) & 0xffu));

// Paleta del HUD (playfield separado en la franja inferior): 4 planos -> 16
// colores propios, distintos del mapa (fondo negro, texto blanco, acentos).
constexpr eng::u16 kHudPalette[16] {
    0x000, 0xfff, 0xf00, 0x0f0, 0x00f, 0xff0, 0x0ff, 0xf0f,
    0x844, 0x884, 0x448, 0x444, 0x222, 0x666, 0xaaa, 0xddd,
};

// Paleta emergente del muestrario: PF1 (0-7), PF2 (8-15) y sprites 16-19.
// En single (regresión) los 16 primeros bastan; el sprite usa COLOR16-19.
constexpr eng::u16 kScenePalette[32] {
    // PF1 (primer plano): registros 0..7. Color 0 = transparente.
    0x000, 0xf0c, 0x0cf, 0xff0, 0xf80, 0x84f, 0xf44, 0xfff,
    // PF2 (fondo): registros 8..15. Color 0 (reg 8) = transparente.
    0x000, 0x021, 0x063, 0x0a5, 0x2d7, 0xdfa, 0xce7, 0xfff,
    // Sprite (COLOR16-19): borde(17) ambar, interior(19) blanco, 16 rojo.
    0xf00, 0xff8, 0x4aa, 0xfff, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
};

eng::u16 g_map_cells[kMapTilesX * kMapTilesY] {};

void build_map(eng::Span<eng::u16> cells, eng::u32 seed) {
    for (eng::u32 y = 0; y < kMapTilesY; ++y) {
        for (eng::u32 x = 0; x < kMapTilesX; ++x) {
            const eng::u32 h = demo::cell_hash(x, y, seed);
            cells.at(y * kMapTilesX + x) = static_cast<eng::u16>(h & 63u);
        }
    }
}

// Mapa COHERENTE tipo nivel: bandas horizontales de hierba/muro/agua/camino con
// pocos tiles repetidos (ideal para medir el skip-on-equal del engine).
void build_map_coherent(eng::Span<eng::u16> cells) {
    for (eng::u32 y = 0; y < kMapTilesY; ++y) {
        const eng::u16 band = static_cast<eng::u16>(y / 16u);
        for (eng::u32 x = 0; x < kMapTilesX; ++x) {
            eng::u16 t;
            if (band < 4u) t = 50;                 // hierba
            else if (band < 6u) t = 36;            // banda de muro
            else if (band < 14u) t = 5;            // agua central
            else t = 22;                           // camino
            if (((x + y) % 97u) == 0u) t = 41;     // arbusto suelto (variedad)
            cells.at(y * kMapTilesX + x) = t;
        }
    }
}

// Construye el mapa del FG (PF1) en modo DPF: un checkerboard sobre el mapa de
// fondo donde la mitad de los tiles son el tile 0 (glyph 0), que `fg_row`
// devuelve VACÍO → PF1 no escribe nada ahí y PF2 (fondo opaco) se ve a través.
// Es la "mitad de tiles transparentes" del muestrario DPF con tiles.
// SIN divisiones: (x+y)&1 usa dos contadores; el índice es y*W+x (una mult).
void build_fg_checkerboard_map(eng::Span<eng::u16> fg, const eng::Span<const eng::u16> bg) {
    for (eng::u32 y = 0; y < kMapTilesY; ++y) {
        const eng::u32 row = y * kMapTilesX;
        for (eng::u32 x = 0; x < kMapTilesX; ++x) {
            fg[row + x] = (((x + y) & 1u) != 0u) ? 0u : bg[row + x];
        }
    }
}

// Necesidad de pre-scroll de cada fase del ciclo (1..7) para que las direcciones
// con componente negativa (izquierda/arriba) tengan recorrido antes de tocar el
// borde 0. Fase 0 (H derecha) y 2 (V abajo) arrancan desde 0 sin problema.
constexpr bool phase_needs_pre_x(eng::u8 phase) {
    return phase == 1 || phase == 5 || phase == 6 || phase == 7; // H izq, HV izq, diagonales
}
constexpr bool phase_needs_pre_y(eng::u8 phase) {
    return phase == 3 || phase == 5 || phase == 6 || phase == 7; // V arr, HV arr, diagonales
}

struct DemoGame {
    field::XlimitedScene<kScrollConsts> scene {}; // escena reutilizable (geometría NTTP)
    field::XlimitedSceneConfig scene_cfg {};
    eng::graphics::FramePlan plan {};
    eng::MemoryBlock m_bob {};           // BOB enmascarado de prueba (1 plano 16x16 + máscara)
    eng::MemoryBlock m_fg_map {};        // mapa del FG (checkerboard transparente) en el arena
    // --- Ciclo de vida del muestrario (técnicas 1..9) -----------------------
    PatternMode m_pattern = PDualPatterns; // técnica activa (modo de movimiento)
    eng::u8 m_current = 0;              // índice de la técnica activa (0..N-1)
    eng::u8 m_prevIn = 0;               // último estado de entrada (edge)
    eng::amiga::KeyboardState m_kbd;    // estado del puerto serie del teclado

    /// PARAR la técnica saliente + INICIAR la entrante (ciclo de vida del
    /// efecto): reinicia la cámara de ambos playfields y la fase del auto-ciclo,
    /// y aplica paso/patrón de la nueva técnica. La geometría (NTTP ScrollConsts)
    /// no se re-configura: las técnicas son comportamiento (patrón × velocidad).
    void apply_tech(eng::u8 idx) {
        if (idx >= kTechCount) return;
        m_current = idx;
        const Technique& t = kTechniques[idx];
        // STOP
        scene.bg().reset_scroll();
        if (kDual) scene.fg().reset_scroll();
        scene.reset_auto();
        // START
        m_pattern = t.pattern;
        const eng::u8 stB = (t.pattern == PDualPatterns) ? t.stepBg : (t.step ? t.step : kStepBg);
        const eng::u8 stF = (t.pattern == PDualPatterns) ? t.stepFg : (t.step ? t.step : kStepMax);
        scene.bg().set_scroll_step(stB ? stB : 1u);
        scene.fg().set_scroll_step(stF ? stF : 1u);
    }

    /// Consume la entrada (hardware directo: un botón/joystick avanza técnica; el
    /// gancho `g_tech_new` 1..9 selecciona una específica) y el auto-ciclo.
    void poll_tech_switch(eng::u32 frame) {
        // Entrada por hardware (CIA): cualquier botón/dirección de un puerto de
        // juego → siguiente técnica (borde de subida). Ventana de arranque: el
        // emulador deja las líneas sin joystick a 0 (="pulsado") el primer frame
        // y avanzaría de técnica espuriamente; se ignoran los bordes hasta que
        // el puerto haya mostrado al menos un frame en reposo.
        eng::amiga::GameInput gin;
        eng::amiga::poll_input(gin);
        const eng::u8 in = static_cast<eng::u8>(gin.port0 | gin.port1);
        if (frame >= 2u && in != 0u && m_prevIn == 0u) {
            m_current = static_cast<eng::u8>((static_cast<eng::u16>(m_current) + 1u) % (kTechCount ? kTechCount : 1u));
            apply_tech(m_current);
        }
        m_prevIn = in;
        // Teclado por puerto serie de CIAA: '1'..'6' (scancodes 0x02..0x07)
        // seleccionan la técnica concreta, igual que el gancho `g_tech_new`.
        eng::amiga::poll_keyboard(m_kbd);
        if (m_kbd.pending != 0u) {
            const eng::u8 pd = m_kbd.pending;
            m_kbd.pending = 0u;
            if (pd >= 0x02u && pd <= (0x02u + kTechCount - 1u)) {
                apply_tech(static_cast<eng::u8>(pd - 0x02u));
            }
        }
        // Gancho directo 1..9 (herramientas/host).
        const eng::u8 want = g_tech_new;
        if (want != 0u && want <= kTechCount) { g_tech_new = 0; apply_tech(static_cast<eng::u8>(want - 1u)); }
#if K_AUTOCYCLE
        if (frame != 0u && (frame % K_AUTOCYCLE) == 0u) {
            const eng::u32 nxt = (frame / K_AUTOCYCLE) % (kTechCount ? kTechCount : 1u);
            apply_tech(static_cast<eng::u8>(nxt));
        }
#else
        (void)frame;
#endif
    }
    bool ready = false;

// Generadores de filas de tile (incrustan base de color y transparencia).
    static eng::u16 fg_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
        // PF1: base 0; en DPF fondo transparente (tramado) para que PF2 se vea.
        // glyph 0 = tile transparente del checkerboard SOLO en DPF: PF1 no
        // escribe (vacío) y PF2 se ve. En single (regresión) se dibuja normal
        // para que la línea base no tenga agujeros negros.
        if (glyph == 0 && kDual) return 0;
        return demo::pf_plane_row(glyph, variant, row, plane, 0, kDual);
    }
    static eng::u16 bg_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
        return demo::pf_plane_row(glyph, variant, row, plane, 8, false); // PF2 opaco
    }

    void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
        eng::debug::mark_init_started(g_eng_run_status);
        eng::debug::reset(g_eng_frame_telemetry);
        // La reserva es el bloque Chip contiguo total; el uso real (~145 KB en DPF,
        // ~100 KB single) cabe en 360 KB sin acercarse al límite del A500.
        if (!backend.configure_memory({360u * 1024u, 16u * 1024u, 8u * 1024u})) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010701u);
            return;
        }

// Mapa de fondo (PF2). En DPF, PF1 usa un mapa FG con la mitad de tiles
        // transparentes (g_fg_map_cells, checkerboard sobre tile 0). El mapa FG
        // se reserva en el arena Chip (no en BSS) para no duplicar ~140 KB.
        if (K_COHERENT != 0) {
            build_map_coherent(eng::Span<eng::u16>::from_raw(g_map_cells, kMapTilesX * kMapTilesY));
        } else {
            build_map(eng::Span<eng::u16>::from_raw(g_map_cells, kMapTilesX * kMapTilesY), 0x13579bdu);
        }
        if (kDual) {
            const eng::u32 map_bytes = static_cast<eng::u32>(kMapTilesX) * kMapTilesY * 2u;
            m_fg_map = backend.memory().chip.allocate(map_bytes, 2);
            if (!m_fg_map.valid()) {
                eng::debug::mark_failed(g_eng_run_status, 0x00010702u);
                return;
            }
            build_fg_checkerboard_map(
                eng::Span<eng::u16>::from_raw(static_cast<eng::u16*>(m_fg_map.data), kMapTilesX * kMapTilesY),
                eng::Span<const eng::u16>::from_raw(g_map_cells, kMapTilesX * kMapTilesY));
        }

        scene_cfg.viewport_w = kViewportW;
        scene_cfg.viewport_h = kViewportH;
        scene_cfg.tile_width = kTileWidth;
        scene_cfg.tile_height = kTileSize;
        scene_cfg.planes = kEffectivePlanes;   // 4 single, 3 por playfield en DPF
        scene_cfg.fetch_mode = kFetchMode;
        // PF1 (FG) usa el mapa checkerboard en DPF, o el mapa normal en single.
        {
            const eng::u16* fg_cells = kDual && m_fg_map.valid()
                ? static_cast<const eng::u16*>(m_fg_map.data)
                : static_cast<const eng::u16*>(g_map_cells);
            scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(fg_cells, kMapTilesX * kMapTilesY);
        }
        scene_cfg.map.width = kMapTilesX;
        scene_cfg.map.height = kMapTilesY;
        scene_cfg.map.wrap_x = kMapTilesX;
        scene_cfg.map.wrap_y = kMapTilesY;
        scene_cfg.map.edge_tile = kDual ? 0 : 0;
        scene_cfg.tileset_count = kTileCount;
        scene_cfg.fg_row_fn = &DemoGame::fg_row;
        scene_cfg.bg_row_fn = &DemoGame::bg_row;
        scene_cfg.dual = kDual;
        if (kDual) {
            // PF2 (fondo) opaco con su propio mapa (el completo, sin checkerboard).
            scene_cfg.map2.cells = eng::Span<const eng::u16>::from_raw(g_map_cells, kMapTilesX * kMapTilesY);
            scene_cfg.map2.width = kMapTilesX;
            scene_cfg.map2.height = kMapTilesY;
            scene_cfg.map2.wrap_x = kMapTilesX;
            scene_cfg.map2.wrap_y = kMapTilesY;
        }
        // Salto configurable (px/frame máximos por eje; sub-pasos atómicos de 1 px,
// paint-then-advance → nunca a medio pintar) + DPF con patrones por playfield.
scene_cfg.max_step = kStepMax;
        // En dual_patterns se ajusta el paso por playfield (la demo lo hace
        // justo después de begin con set_scroll_step).
        scene_cfg.parallax_x = kParallax;
        scene_cfg.linear_display = kLinear;
        scene_cfg.effect = kEffectMode;
        scene_cfg.start_phase = kStartPhase;
        scene_cfg.phase_frames = kPhaseFrames;
        scene_cfg.pre_scroll = kPreScroll;
        scene_cfg.palette = kScenePalette;
#if K_CANVAS_FG
        scene_cfg.dual = true;
        scene_cfg.fg_canvas = true; // FG = lienzo plano (PF2 en DPF)
#endif
#if K_SPRITE
        scene_cfg.sprite_data_bytes = 64; // 1 sprite 16x16, 1 word (DAT+DATB por línea)
#endif
#if K_HUD
        scene_cfg.hud_height = K_HUD_HEIGHT;
        scene_cfg.hud_planes = K_HUD_PLANES;
        scene_cfg.hud_palette = kHudPalette;
#endif

        if (!scene.begin(backend.memory(), scene_cfg)) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010703u);
            return;
        }
        // Técnica inicial del muestrario (ciclo de vida): paso/patrón por técnica.
        {
            const eng::u8 start = static_cast<eng::u8>(K_START_TECH % (kTechCount ? kTechCount : 1u));
            apply_tech(start);
        }
        if (!scene.fill(backend, plan)) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010705u);
            return;
        }
#if K_CANVAS_FG
        // FG estático (lienzo plano, DPF): marco + figuras dibujadas UNA vez en
        // init con la superficie del canvas FG. PF2 usa colores 8..15 (base DPF).
        {
            auto fg = scene.canvas_fg_surface();
            fg.draw_line(0, 0, kViewportW - 1, 0, 1);              // borde sup
            fg.draw_line(0, kViewportH - 1, kViewportW - 1, kViewportH - 1, 1); // borde inf
            fg.fill_rect(24, 24, 16, 16, 2);                       // caja
            fg.draw_line(60, 40, 200, 120, 3);                     // diagonal
            fg.fill_rect(kViewportW - 40, kViewportH - 40, 24, 24, 1); // caja abajo-derecha
        }
#endif
#if K_HUD
        // Dibuja el HUD UNA VEZ en init (boot, sin competir con el DMA) con la
        // superficie del lienzo HUD. El display del corkscrew aplica un offset de
        // fetch horizontal al playfield de la franja (los primeros ~16 px y el
        // borde derecho pueden recortarse), así que el contenido se dibuja en la
        // zona central del lienzo.
        {
            auto hud = scene.hud_surface();
            // Fondo oscuro NO negro (0x844): el check de negro interno del
            // harness rechaza negro dentro del rect activo.
            hud.fill_rect(0, 0, kViewportW, K_HUD_HEIGHT, 8);
            hud.fill_rect(80, 6, 48, 20, 1);                       // barra blanca (marco)
            hud.fill_rect(84, 10, 40, 12, 2);                      // interior rojo
            hud.draw_line(140, 6, 200, 25, 3);                     // diagonal verde
            hud.set_pixel(220, 6, 15);                             // píxel claro
            hud.set_pixel(220, 25, 15);
        }
#endif
// Pre-scroll para fases con componente negativa (izquierda/arriba) o diagonales.
        if (kDual && kDualPatterns) {
            // El BG diagonal y el FG Lissajous necesitan recorrido en AMBOS ejes
            // desde el inicio (podrían mover a la izquierda/arriba de primeras):
            // pre-scroll a la derecha/abajo deja margen para toda la figura.
            if (!scene.pre_scroll(backend, plan, 256, 256)) {
                eng::debug::mark_failed(g_eng_run_status, 0x000107e2u);
                return;
            }
        } else {
            const eng::u8 startPhase = (kEffectMode == 0) ? kStartPhase
                : static_cast<eng::u8>(kEffectMode - 1);
            const bool needX = phase_needs_pre_x(startPhase);
            const bool needY = phase_needs_pre_y(startPhase);
            if ((needX || needY) &&
                !scene.pre_scroll(backend, plan, needX ? kPreScroll : 0, needY ? kPreScroll : 0)) {
                eng::debug::mark_failed(g_eng_run_status, 0x000107e1u);
                return;
            }
        }
        if (!scene.compose()) {
            eng::debug::mark_failed(g_eng_run_status, 0x00010708u);
            return;
        }
        scene.install(backend);

#if K_SPRITE
        // Sprite hardware (diamante 16x16, 4 colores via COLOR16-19): DAT=plano0,
        // DATB=plano1. El interior usa COLOR19 (ambos planos), el borde COLOR17.
        {
            auto& sp = scene.sprites();
            eng::Span<eng::u8> sd = sp.sprite_data(); // 64 bytes (16 líneas × 2 words)
            eng::u16* data = reinterpret_cast<eng::u16*>(sd.data());
            for (int y = 0; y < 16; ++y) {
                eng::u16 a = 0, b = 0;
                const int half = (y < 8) ? y : 15 - y; // 0..7..0 (diamante)
                for (int x = 0; x <= half; ++x) {
                    const eng::u16 bitA = static_cast<eng::u16>(0x8000u >> (7 + x));
                    const eng::u16 bitB = static_cast<eng::u16>(0x8000u >> (7 - x));
                    a |= bitA | bitB;
                    b |= bitA | bitB;
                }
                data[y * 2] = a;
                data[y * 2 + 1] = b;
            }
            sp.set(0, {true, eng::Span<const eng::u16>::from_raw(data, 32), 1, 16, 120, 96, 111, 16});
        }
#endif

#if K_HUD
        // BOB enmascarado de prueba: 4 planos 16x16 (blanco, origen Chip RAM) +
        // máscara de 1 bit con un agujero central 4x4 (px 6..9). El cookie-cut
        // escribe blanco donde la máscara es 1 y conserva el mapa donde es 0:
        // bloque blanco 16x16 con un hueco que deja ver el fondo.
        m_bob = backend.memory().chip.allocate(160, 16); // 4*32 + 32 (máscara)
        if (m_bob.valid()) {
            eng::u8* bob = static_cast<eng::u8*>(m_bob.data);
            for (eng::u32 i = 0; i < 128; ++i) bob[i] = 0xff;   // 4 planos = blanco
            for (eng::u16 r = 0; r < 16; ++r) {                 // máscara: hueco px 6..9
                const eng::u16 mw = (r >= 6 && r < 10) ? static_cast<eng::u16>(0xfc3fu) : static_cast<eng::u16>(0xffffu);
                bob[128 + r * 2] = static_cast<eng::u8>(mw >> 8);
                bob[128 + r * 2 + 1] = static_cast<eng::u8>(mw & 0xff);
            }
        }
#endif

        ready = true;
        eng::debug::mark_ready(g_eng_run_status, 0x10700000u);
    }

    // -------------------------------------------------------------------------
    // HUD de ejemplo: objeto FIJO en pantalla dibujado con la capa de dibujo.
    // La ventana visible empieza en `display_offset = (mapposy+tile_height)%DH`,
    // NO en mapposy: por eso la fila de mundo se obtiene con screen_to_world_y
    // (equivale a mapposy()+tile_height()+sy) y la columna con screen_to_world_x
    // (= mapposx()+sx). Así el HUD sigue a la cámara y no se desliza con el mundo.
    // Las primitivas CPU (set_pixel/fill_rect/draw_line) escriben al instante; el
    // BOB enmascarado es un blit que se encola en `plan` (se ejecuta después).
    // -------------------------------------------------------------------------
    // BOB enmascarado de MUNDO (16x16, 4 planos blancos, wx múltiplo de 16):
    // bloque blanco con hueco 4x4 en el centro que deja ver el mapa. Es un
    // objeto del mundo en posición fija (160,100): scrollea con el fondo. Las
    // primitivas de blit requieren wx word-aligned, así que un objeto FIJO en
    // pantalla se dibuja con las primitivas CPU (set_pixel/fill_rect/draw_line).
    void queue_hud_blits(eng::graphics::FramePlan& hud_plan) {
#if K_HUD && K_BOB
        auto& bg = scene.bg();
        if (m_bob.valid()) {
            const eng::u16* bob = static_cast<const eng::u16*>(m_bob.data);
            bg.add_world_bitmap_masked(hud_plan,
                eng::Span<const eng::u16>::from_raw(bob, 64),      // 4 planos × 32 bytes
                eng::Span<const eng::u16>::from_raw(bob + 64, 16), // máscara 1 bit × 32 bytes
                160, 100, 16, 16, 2, 32, 4);
        }
#else
        (void)hud_plan;
#endif
    }

    // Primitivas CPU del HUD fijo. IMPORTANTE: se dibujan en `render()` (durante
    // el vblank, tras wait_vblank) y NO en `update()`: el update corre durante
    // scanlines visibles y las escrituras CPU al framebuffer compiten con el DMA
    // de bitplanes, produciendo blackouts periódicos de un frame.
    // Primitivas CPU del HUD fijo. IMPORTANTE (limitación de bus del Amiga): las
    // escrituras CPU al chip RAM durante el frame visible roban ciclos al DMA de
    // bitplanes; con muchas RMW (fill_rect/draw_line grandes) aparecen scanlines
    // negros periódicos. Por eso aquí SOLO se marcan 2 píxeles; el dibujo masivo
    // (HUD de la franja inferior) va por Blitter o se dibuja una vez en init.
    void draw_hud_cpu() {
#if K_HUD
        auto& bg = scene.bg(); // playfield de scroll (roles, no índice)
        auto s = scene.bg_surface(); // contexto de dibujo con clip (mundo)
        const eng::s32 y0 = bg.screen_to_world_y(4);
        s.set_pixel(bg.screen_to_world_x(60), y0, 2);      // píxel cian
        s.set_pixel(bg.screen_to_world_x(60), y0 + 7, 2);  // píxel cian
#endif
    }

    void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
        eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
        if (!ready) return;

        plan.clear();
        plan.set_blit_budget_limits({8192, 16384, 4, 120});
        // Conmutación de técnica (gancho de entrada / auto-ciclo). Reinicia el
        // scroll de la técnica saliente e inicia la entrante (ciclo de vida).
        poll_tech_switch(context.frame.frame_index);
        // Avanza el camino configurado (1 px/frame). Si una fase choca con el
        // borde 0 (dirección inversa sin recorrido) simplemente no avanza.
        eng::s32 dx = 0, dy = 0;
        if (m_pattern == P8Way || !kDual) {
            scene.update_auto(plan, context.frame.frame_index, dx, dy);
        } else {
            // PATRONES LOCALES de la demo (no viven en el engine):
            //   BG visual (PF2, `fg()`): diagonal con velocidad sinusoidal
            //     (acelera→máx→frena→revierte).  FG visual (PF1, `bg()`): Lissajous
            //     en X con offset inicial.  La Y es COMPARTIDA (el compositor DPF
            //     usa un único split de Copper: ambos playfields deben llevar el
            //     mismo mapposy por frame). Cada campo aplica su max_step como
            //     sub-pasos atómicos (paint-then-advance).
            const eng::u32 t  = (context.frame.frame_index >> 2) & 63u;
            const eng::s32 vy = demo::kSin64[t] / 8;                      // Y compartido
            const eng::s32 vxBg = demo::kSin64[(t + 8u) & 63u] / 8;      // BG diag.
            const eng::u32 ph = context.frame.frame_index + kLissajousOffset;
            const eng::s32 vxFg = demo::kSin64[ph & 63u] / 8;            // FG Lissajous
            bool ok = scene.fg().update_scroll(plan, vxBg, vy);          // BG visual
            if (ok) ok = scene.bg().update_scroll(plan, vxFg, vy);       // FG visual
            if (!ok) { /* borde sin recorrido: no avanza este frame */ }
        }

        // HUD: el BOB (blit) se encola en el plan y se ejecuta con el scroll.
        // Las primitivas CPU van a `render()` (vblank) para no competir con el DMA.
        queue_hud_blits(plan);

        if (!backend.execute_frame_plan(plan)) {
            ready = false;
            eng::debug::mark_failed(g_eng_run_status, 0x0001070cu);
            return;
        }
        if (!scene.compose()) {
            ready = false;
            eng::debug::mark_failed(g_eng_run_status, 0x0001070du);
            return;
        }

        // Telemetría de carga por frame (leer por canal lateral `mem <addr> <len>`).
        // Supervisa la homogeneidad: blit_jobs debe ser ~constante salvo los picos
        // de fillup en los cruces de tile (fillup_extra).
        {
            auto& tel = g_eng_frame_telemetry;
            tel.frame = context.frame.frame_index;
            const eng::u32 w = plan.blit_budget().words;
            tel.blit_jobs = plan.blit_job_count();
            tel.blit_words = static_cast<eng::u16>(w > 0xffffu ? 0xffffu : w);
            tel.copper_words = scene.copper_words();
            tel.fillup_extra = 0;
        }

        // Telemetría: mapposx en bytes bajos, videoposx en altos, BPLCON1 en medio;
        // byte superior: 0x10 (marcador 107) | técnica activa en el nibble bajo.
        const auto& f = scene.bg();
        auto view = f.hardware_view();
        const eng::u32 marker = ((0x10u | (m_current & 0x0fu)) << 24) |
            (static_cast<eng::u32>(f.mapposx() & 0xff)) |
            (static_cast<eng::u32>(view.bplcon1 & 0xff) << 8) |
            (static_cast<eng::u32>(f.videoposx() & 0xff) << 16);
        eng::debug::mark_ready(g_eng_run_status, marker);
    }

    void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
        if (ready) {
            scene.install(backend);
            draw_hud_cpu(); // primitivas CPU durante el vblank (seguro)
        }
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
