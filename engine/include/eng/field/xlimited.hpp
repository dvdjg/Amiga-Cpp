#pragma once

/// \file xlimited.hpp
/// Implementación fiel del algoritmo **X-Limited** de Georg Steger
/// (ScrollingTricks / Scroller_XLimited) sin arrastrar la máquina circular
/// de `TileFieldController`.
///
/// Este header es *didáctico*: cada sección explica el invariante del
/// hardware que mantiene y por qué una alternativa aparentemente más simple
/// costaría más CPU/Blitter/Chip RAM. Todos los comentarios tutoriales están
/// en español con ortografía correcta.
///
/// -----------------------------------------------------------------------------
/// 1. Geometría canónica parametrizada (fuentes primarias: xlimited.c:45-73, weiju/xlimited.c:68)
/// -----------------------------------------------------------------------------
///
///   SCREENWIDTH  = cfg.viewport_w píxeles visibles (defecto 320; caso alternativo 288).
///   SCREENHEIGHT = cfg.viewport_h píxeles visibles (defecto 256; caso alternativo 224).
///   EXTRAWIDTH   = 32 (modo 1x/2x fetch) ó 64 (modo 4x, fetch 64 píxeles).
///   BITMAPWIDTH  = SCREENWIDTH + EXTRAWIDTH (ej. viewport 320→352 ó 384; viewport 288→320 ó 352).
///   BITMAPBYTESPERROW = BITMAPWIDTH / 8.
///   BLOCKWIDTH/BLOCKHEIGHT = cfg.tile_width / cfg.tile_height (defecto 16).
///   BITMAPBLOCKSPERROW = BITMAPWIDTH / BLOCKWIDTH (ej. 352/16=22, 384/16=24, 320/16=20).
///   BITMAPBLOCKSPERCOL = SCREENHEIGHT / BLOCKHEIGHT (ej. cfg.viewport_h / cfg.tile_height).
///   BLOCKPLANELINES = BLOCKHEIGHT * planes (ej. cfg.tile_height*planes → 48/64/80/96).
///   BITMAPPLANELINES = BITMAPHEIGHT * planes (altura total en planeline).
///
///   Altura total del bitmap (xlimited.c:68, weiju/xlimited.c:68, xylimited.c:73):
///
///     display_height = viewport_h + (scroll_y ? 2*BLOCKHEIGHT : 0)   // corkscrew: +EXTRAHEIGHT
///     bitmapheight   = display_height
///                    + (map_width / BITMAPBLOCKSPERROW / planes) + 1
///                    + 3                               // guarda + fetch ancho
///                    (si scroll_y se redondea a múltiplo de BLOCKHEIGHT para que
///                     la última banda de staging quepa en Chip RAM)
///
///   `BITMAPHEIGHT` del original depende del algoritmo (errata corregida):
///   X-Limited puro usa `BITMAPHEIGHT = SCREENHEIGHT`; el corkscrew/XY usa
///   `BITMAPHEIGHT = SCREENHEIGHT + EXTRAHEIGHT` (EXTRAHEIGHT = 2*BLOCKHEIGHT),
///   que es la **banda de staging** de 2 bloques donde se pre-pinta la
///   fila/columna entrante antes de que el display la alcance al envolver en
///   `display_height` (ver §13 de docs/architecture/AMIGA_8WAY_SCROLLING.md).
///   En el engine `cfg.scroll_y` selecciona el corkscrew (demo 107).
///
///   El término `map_width / BITMAPBLOCKSPERROW / planes` es el número de
///   planeline extra necesarias para que el desplazamiento horizontal sin fin
///   quepa sin salir de Chip RAM. Cada cfg.tile_width píxeles de scroll horizontal se avanza
///   cfg.tile_width/8 bytes en el puntero de bitplanes; sin altura extra, ese avance
///   dejaría de ser contiguo al envolver el borde derecho del bitmap. Con
///   interleaved, el wrap es *vertical*: al llegar al final de una planeline,
///   el siguiente byte contiguo está 1 planeline más abajo (ver §§2–3).
///   `+1` es la planeline de guarda del Blitter y `+3` es el margen para el
///   fetch ancho de 42/48 bytes (DDFSTRT = $30, ver §4).
///
///   Espacio virtual parametrizado (16×16 pantallas por defecto):
///     map_w = cfg.screens_x * (cfg.viewport_w / cfg.tile_width)
///     map_h = cfg.screens_y * (cfg.viewport_h / cfg.tile_height)
///   Si cfg.map.width/height ya viene dado, se respeta; en caso contrario se deriva
///   de screens_x/y y viewport/tile. Caso 288×224 → viewport 288×224 con 18×14 tiles
///   por pantalla y mapa 16×16 pantallas = 288×224 tiles (4608×3584 px) si tile 16.
///
/// -----------------------------------------------------------------------------
/// 2. Por qué **interleaved es obligatorio** (ScrollingTricks/Docs/interleaved)
/// -----------------------------------------------------------------------------
///
///   En un bitmap *no interleaved* (planar clásico) cada plano ocupa un bloque
///   contiguo: plano 0 completo, plano 1 completo, etc. El Blitter sólo conoce
///   módulos por plano (BLTAMOD/BPLMOD), de modo que para copiar un bloque de
///   cfg.tile_width×cfg.tile_height en planes necesitaría planes operaciones independientes
///   y el wrap horizontal requeriría un copy adicional por plano.
///
///   En un bitmap **interleaved** (BMF_INTERLEAVED) la memoria se organiza así:
///
///     línea 0 plano 0, línea 0 plano 1, … línea 0 plano N-1,
///     línea 1 plano 0, línea 1 plano 1, …
///
///   Cada planeline tiene BITMAPBYTESPERROW bytes (ej. 44 para 352, 48 para 384, 40 para 320).
///   Desde el punto de
///   vista del Blitter, todo el bitmap es *una sola columna* de
///   BITMAPPLANELINES planeline. Un bloque de cfg.tile_width×cfg.tile_height ocupa
///   BLOCKPLANELINES = cfg.tile_height*planes planeline contiguas, de modo que
///   **un único blit** (`bltsize = BLOCKPLANELINES*64 + words`) copia los `planes`
///   (3..6, ej. 48/64/80/96 planelíneas para cfg.tile_height=16*3/4/5/6) a la vez con un solo
///   setup de registros. Esto es exactamente la mitad de
///   coste que Scroller_XUnlimited (que necesitaba doble blit) y elimina casi
///   todo el flicker: el Blitter nunca deja los planos a medias.
///
///   Esquema ASCII (ej. 4 planos, BITMAPWIDTH=352, 44 bytes por planeline;
///   genérico: `BITMAPBYTESPERROW*planes` bytes por scanline interleaved, ej. 44*3=132, 44*4=176, 44*5=220, 44*6=264):
///
///     dirección 0:   [L0 P0 44B][L0 P1 44B][L0 P2 44B][L0 P3 44B]
///     dirección 176: [L1 P0 44B][L1 P1 44B]...
///     ...
///     La CPU ve el bitmap como `frontbuffer + y*BITMAPBYTESPERROW + x`
///     donde `y` es índice de planeline y `x` es byte word-aligned.
///     Cada incremento de `y` avanza 1 planeline (`BITMAPBYTESPERROW` bytes),
///     cada incremento de `x` avanza 2 B (1 word)
///     dentro de la planeline. El coste por scanline interleaved es
///     `BITMAPBYTESPERROW*planes` (ej. 44*3=132, 44*4=176, 44*5=220, 44*6=264).
///
///   Sin interleaved, la fórmula `frontbuffer + y*BITMAPBYTESPERROW + x`
///   sería inválida: `y` tendría que ser fila de píxel y habría que sumar
///   `plane*plane_bytes`. El algoritmo de Steger colapsa ambas dimensiones en
///   una sola coordenada de planeline, y eso **solo** funciona si el bitmap
///   se reservó con `BMF_INTERLEAVED`.
///
/// -----------------------------------------------------------------------------
/// 3. Por qué el wrap horizontal **no necesita Copper segmentado**
/// -----------------------------------------------------------------------------
///
///   En el truco circular de `TileFieldController` el viewport es una ventana
///   que se recentra y el compositor usa un split de Copper para mostrar dos
///   mitades del bitmap en una misma línea de raster cuando la ventana cruza
///   el borde de la superficie. Ese split cuesta una espera de Copper por
///   frame y obliga a mantener dos punteros por plano.
///
///   XLimited no necesita ese split. El bitmap es EXTRAWIDTH píxeles (ej. 2 bloques) más
///   ancho que la ventana visible. El Copper siempre fetcha 42 bytes por
///   línea (DDFSTRT=$30, DDFSTOP=$D0): 40 visibles + 2 de margen. El puntero
///   de bitplanes (`BPLxPT`) avanza de forma *lineal* con el scroll y el
///   desplazamiento fino (`BPLCON1`) corrige los 0..(cfg.tile_width-1) píxeles restantes.
///
///   Cuando el área visible `videoposx … videoposx+cfg.viewport_w` cruza el borde derecho
///   del bitmap (cfg.bitmap_width), el siguiente byte que fetcha el Agnus no es el principio
///   de la misma planeline, sino el byte 0 de la **siguiente planeline**.
///   Como el bitmap es interleaved, esa siguiente planeline pertenece al
///   siguiente plano de la misma fila de píxel, y el contenido que allí se
///   bliteó fue escrito *plane-shifted* (ver §5). El resultado es que el
///   wrap se ve continuo sin tocar el Copper: una sola lista, sin wait
///   intermedio, y sin `surface_origin`.
///
///   En resumen: el Copper segmentado es necesario cuando la superficie es
///   circular y el viewport debe saltar. XLimited evita el salto haciendo que
///   el bitmap sea ligeramente más ancho y dejando que el fetch lineal haga el
///   wrap vertical automáticamente. El coste es esa columna extra de tiles y
///   el blit plane-shifted, mucho más barato que un segundo Copper wait por
///   línea.
///
///   **El wrap vertical SÍ necesita un split de Copper** (corkscrew/XYLimited):
///   el display envuelve en `display_height = viewport_h + 2*tile_height`, y al
///   llegar a `split_line = display_height - display_offset` filas dentro de la
///   ventana los punteros vuelven a la fila 0 (`real_base + planeaddx + p*row_bytes`).
///   Ver `scroll_down/up/right/left` y `XlimitedDisplayComposer::emit_full`.
///
/// -----------------------------------------------------------------------------
/// 4. Fetch y registros del Copper (xlimited.c:389-430, hardware.c)
/// -----------------------------------------------------------------------------
///
///   Para DDFSTRT=$30 el Agnus fetcha 42 bytes por línea (20 words + 2 de
///   margen). Los módulos son:
///
///     BPL1MOD = BPL2MOD = BITMAPBYTESPERROW * planes - SCREENBYTESPERROW - modulo_offset
///     donde SCREENBYTESPERROW = cfg.viewport_w / 8
///
///   (para fetch normal, `modulooffset=2`; para BPL32 es 4 y bitmapoffset 16 B,
///   para BPL32+BPAGEM es 8 y 48 B). El compositor usa siempre el caso normal
///   (2, offset 0) salvo que la demo pida explícitamente fetch ancho.
///
///   El desplazamiento fino se calcula cada frame (xlimited.c:201, UpdateCopperlist):
///
///     I        = fetchinfo[fetchmode].scrollpixels  // 16, 32 ó 64
///     xpos     = videoposx + I - 1
///     planeaddx = (xpos / I) * (I/8)                // coarse en bytes
///     fine     = (I-1) - (xpos & (I-1))
///     scroll   = (fine & 15)*0x11                  // duplica nibble PF1/PF2
///     si fine & 16  scroll |= 0x4400
///     si fine & 32  scroll |= 0x8800
///     BPLCON1  = scroll
///     BPLxPT   = Planes[l] + planeaddx
///
///   Con I=cfg.tile_width (16 por defecto), `planeaddx = (videoposx+tile_width-1)/tile_width*2` y
///   `scroll = (tile_width-1 - videoposx&(tile_width-1))&15`
///   replicado en ambos nibbles, que es la fórmula canónica `BPLCON1=(tile_width-fine)&(tile_width-1)`.
///
/// -----------------------------------------------------------------------------
/// 5. Columna entrante y guarda de 1 word (xlimited.c:448-553)
/// -----------------------------------------------------------------------------
///
///   Cada píxel desplazado es como máximo la copia de un tile de cfg.tile_width×cfg.tile_height
///   en la columna (ver §6). Tras cfg.tile_width px horizontales se habrá dibujado una
///   columna completa de cfg.viewport_h/cfg.tile_height tiles que entrará visible en offset cfg.tile_width. En
///   interleaved ese tile son `BLOCKPLANELINES = BLOCKHEIGHT*planes` planeline
///   (ej. cfg.tile_height*planes → 48/64/80/96) en un único blit.
///   La columna entrante es:
///
///     mapx = mapposx / cfg.tile_width + BITMAPBLOCKSPERROW   (scroll derecha)
///     mapx = mapposx / cfg.tile_width                         (scroll izquierda)
///     mapy = mapposx & (cfg.tile_width-1)
///     x    = BITMAPWIDTH + (videoposx & ~(cfg.tile_width-1))     (derecha, plane-shifted)
///     x    = videoposx & ~(cfg.tile_width-1)                      (izquierda)
///     y    = mapy * BLOCKPLANELINES              // planeline
///
///   El bloque se copia con `BLOCKPLANELINES*64 + words` (words = tile_width/cfg.tile_width unit).
///   En scroll a la derecha la copia es plane-shifted: al sumar BITMAPWIDTH al
///   `x`, el Blitter escribe 1 planeline más abajo (ver docs/xlimited-uk.html,
///   figura xlimited4). Eso deja un hueco de 1 word (2 bytes) que es la primera
///   planeline del bloque siguiente. Para evitar tearing al cambiar de dirección,
///   el código guarda esa word antes de blitear y la restaura si la dirección
///   cambia:
///
///     savewordpointer = frontbuffer + y*BITMAPBYTESPERROW + x/8   (izquierda)
///     savewordpointer = frontbuffer + (y+BLOCKPLANELINES-1)*BITMAPBYTESPERROW + x/8 (derecha)
///
///   Mientras se scrollea en la misma dirección no se restaura; sólo al invertir
///   el sentido se copia `*savewordpointer = saveword`. El coste es una word
///   por frame como máximo, frente a re-blitear toda la columna.
///
/// -----------------------------------------------------------------------------
/// 6. draw_block — contrato exacto del Blitter (xlimited.c:201-227)
/// -----------------------------------------------------------------------------
///
///   draw_block(x, y, mapx, mapy):
///     x = (x/8) & 0xFFFE           // word-aligned
///     y = y * BITMAPBYTESPERROW
///     block = mapdata[mapy*mapwidth + mapx]
///     mapx_src = (block % 20) * 2
///     mapy_src = (block / 20) * (BLOCKPLANELINES * 40)
///     bltcon0 = 0x09F0 (A→D), bltcon1=0, bltafwm/bltalwm=0xFFFF
///     bltamod = 40-2, bltdmod = BITMAPBYTESPERROW-2
///     bltapt  = blocksbuffer + mapy_src + mapx_src
///     bltdpt  = frontbuffer + y + x
///     bltsize = BLOCKPLANELINES*64 + 1
///
///   En el engine este contrato se traduce a un único `BlitJob` TileBlockCopy
///   con `words_per_row = tile_width/cfg.tile_width`, `height = BLOCKPLANELINES`,
///   `source_modulo = 40-2` (o `tile_width/8` generalizado) y
///   `destination_modulo = BITMAPBYTESPERROW - tile_width/8`.
///
///   No se reutiliza la lógica circular de `tile_field.hpp`: no hay
///   `surface_origin`, ni bandas, ni recentrado. Puede compartir `TileLayerMap`
///   (índice → tile) pero el posicionamiento físico es el de Steger.
///
///   Referencias: docs/architecture/AMIGA_8WAY_SCROLLING.md (contraste con el
///   modelo circular), ScrollingTricks/Docs/xlimited-uk.html § “overallbitmapheight”
///
/// -----------------------------------------------------------------------------
/// 7. Micro-parones: por qué ocurren y cómo detectarlos (para no repetirlos)
/// -----------------------------------------------------------------------------
///
///   Dos defectos se vieron en la demo 107 antes de la corrección 30-08-2026:
///
///   a) **Columna con plaquetas repetidas** — `scroll_right/left` usaban
///      `map_tile_y = 0` en lugar de `map_tile_y = mapy_blocks` (mapposx & (tile_width-1)).
///      Como X-Limited dibuja un bloque por píxel en `y = mapy*BLOCKPLANELINES`
///      (cfg.tile_height*planes → 48/64/80/96 según planes 3..6), si siempre
///      se lee la fila 0 del mapa, los cfg.tile_width pasos de un bloque dejan la columna
///      con cfg.tile_width copias del mismo tile. Se detecta visualmente como franja
///      vertical repetida y en `verify-xlimited.mjs` como `map_tile_y != mapy`.
///
///   b) **Micro-parones cada 8 frames con `steps=2`** — 2 px/frame hace que
///      `BPLCON1` salte de 2 en 2 (`0x00,0xEE,0xCC...`) y que el Blitter haga
///      2 blits de `BLOCKPLANELINES` líneas (48/64/80/96 según planes) por frame.
///      Aunque el coste medio cabe en 50 fps,
///      el segundo blit puede cruzar el VBlank y el Copper publica el siguiente
///      `planeaddx` con un frame de retraso, visible como tirón cada 8 frames
///      (cuando `videoposx & (tile_width-1)` envuelve). La forma canónica de Steger es
///      **1 px/frame** y `BPLCON1` cicla `0x00,0x11,0x22...0xFF` sin saltos.
///
///   **Cómo detectarlos sin ver la pantalla** (para una IA que revisa):
///
///   - **Host:** `tools/analyze/verify-xlimited.mjs` — comprueba que
///     `draw_block_job` usa `map_tile_y == mapy` y que `steps` es 1.
///   - **Runtime:** `demos/107_xlimited_corkscrew/analyze-sequence.sh --warp`
///     analiza `out/run/107_xlimited_corkscrew/sequence/frame_*.png` con
///     `tools/analyze/analyze-frame-sequence.sh`: `DuplicatePairs` debe ser 0,
///     `ChangedPairs` 99/100, `MeanDiffAvg` estable (~68) y sin picos de
///     `MaxDiff`. Un micro-parón deja `DuplicatePairs>0` o `MeanDiff` con
///     varianza alta (un frame sin avance, siguiente con doble avance).
///     Además, `BPLCON1` muestreado en el canal lateral debe ciclar 0..(tile_width-1)
///     sin repetir valores (con `steps=2` se ven saltos de 2).
///   - **Periférico:** `eng/debug/peripheral.hpp` expone `0xB70000` con
///     contador de ciclos de Blitter; si `blitter_starts` por frame >1 y el
///     tiempo de Blitter >14 ms, el siguiente `wait_vblank` pierde un VBlank.
///
///   **Instrucciones para no repetir el problema** (checklist para otra IA):
///
///   1. No fijar `map_tile_y` a constante; usar siempre `mapy_blocks` en
///      `scroll_right/left` y documentar que el corkscrew usa `mapy` para
///      mapa y para bitmap.
///   2. Mantener `steps = 1` en `DemoGame::update` para 50 fps; si se necesita
///      2 px/frame, desdoblar en dos frames consecutivos, no en un mismo frame.
///   3. Antes de añadir un segundo campo DPF, medir `m_blitter_starts` y
///      `blit_budget.words` por frame; si `words > 8192` o `jobs>4`, bajar
///      `K_FETCH_MODE` a 0 o reducir `K_TILE_WIDTH` a 16.
///   4. Tras cualquier cambio en `XlimitedConfig`, ejecutar
///      `node tools/analyze/verify-xlimited.mjs` (host) y
///      `bash demos/107_xlimited_corkscrew/analyze-sequence.sh --warp` (runtime)
///      y exigir `OK telemetría`, `OK columna`, `DuplicatePairs==0` y
///      `green>0` en `analyze-demo.sh`.
///
/// \see eng::field::TileLayerMap
/// \see eng::field::XlimitedDisplayComposer

#include <eng/core/fast_div.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/scroll_engine.hpp>
#include <eng/field/tile_field.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

/// Depuración: resultado del `valid()` del compositor dual (0 = OK).
extern volatile eng::u32 g_dbg_dual_valid;


// -----------------------------------------------------------------------------
// Constantes canónicas del algoritmo original (ver §1) — valores por defecto
// para cfg.viewport_w/h = 320/256; el código usa cfg.* en lugar de literales.
// -----------------------------------------------------------------------------
namespace xlimited_detail {
constexpr u16 kScreenW = 320;
constexpr u16 kScreenH = 256;
constexpr u16 kBlock = 16;
constexpr u16 kExtraW32 = 32;
constexpr u16 kExtraW64 = 64;
constexpr u16 kBitmapW32 = 352; // viewport 320+32
constexpr u16 kBitmapW64 = 384; // viewport 320+64
constexpr u16 kBlocksPerRow32 = 22;
constexpr u16 kBlocksPerRow64 = 24;
constexpr u16 kBlocksPerCol = 16; // viewport_h/tile_height por defecto 256/16
constexpr u16 kDdfStrt = 0x0030;
constexpr u16 kDdfStop = 0x00D0;
constexpr u16 kDiwStrt = 0x2981;
constexpr u16 kDiwStop = 0x29C1;

/// DIWSTOP para un viewport de `viewport_h` filas visibles (PAL, inicio en la
/// línea 41). Codificación OCS: el stop vertical es `(diwstop >> 8)` y si su
/// bit 7 está a 0 el hardware suma 256 (WinUAE calcvdiw). Así un viewport más
/// corto (p. ej. 224) deja las filas inferiores FUERA del DIW → se ven como
/// borde (negro, o un HUD dibujado aparte) y no llegan al DAC como scroll.
constexpr u16 diwstop_for_viewport(u16 viewport_h) {
    const u16 vstop = static_cast<u16>(41u + viewport_h);
    return static_cast<u16>(((vstop & 0xffu) << 8) | 0x00c1u);
}
} // namespace xlimited_detail

/// Variante de scroll del playfield XLimited.
///
/// Es una ESPECIALIZACIÓN del scroll: la geometría del corkscrew (banda de
/// staging, walk X, split, saveword) se deriva del modo.
enum class ScrollMode : u8 {
    EightWay = 0,      // corkscrew completo: 8 direcciones, banda de staging 2 bloques,
                       // split vertical, walk X y saveword (direcciones reversas).
    HorizontalOnly = 1,// solo H: display_height = viewport_h (sin banda de staging),
                       // sin split, sin walk vertical. Optimización 1 blit/op.
    VerticalOnly = 2,  // solo V: corkscrew vertical (banda de staging + split), pero
                       // el X no scrollea (no se ejercita el walk X).
    OneDirection = 3,  // 8-way pero sin inversión de dirección: se omite la
                       // restauración de saveword (menos blits en los cruces).
};

/// Configuración de un campo XLimited.
///
/// `map` puede ser cualquier `TileLayerMap` (wrapping opcional). `tileset`
/// apunta a un banco planar en el formato clásico de Steger: un bitmap
/// interleaved de `BLOCKSWIDTH`×`BLOCKSHEIGHT` con
/// `BLOCKSPERROW=20`. Cada bloque ocupa `BLOCKPLANELINES * 2` bytes en cada
/// fila de bloques. Para tiles generados por `demo::build_tile_cache` el
/// layout es compatible si se usa el mismo stride; la demo 107 usa ese
/// helper y adapta el offset de origen en `draw_block`.
///
/// Parametrización viewport/espacio virtual:
///   viewport_w/h definen la ventana visible (320/256 por defecto, 288/224 alternativo).
///   screens_x/y definen el mundo en pantallas (16×16 por defecto → map_w = screens_x*viewport_w/tile_width).
///   Si map.width/height ya viene dado, se respeta; si es 0 se deriva de screens_x/y.
///   bitmap_width = viewport_w + EXTRAWIDTH (32/64 según fetch_mode) si no se fuerza.
///   fill_screen usa visibleRows = viewport_h / tile_height y, con scroll_y
///   (corkscrew), colHeight = BITMAPBLOCKSPERCOL = display_height/tile_height =
///   visibleRows + 2 (pre-rellena la banda de staging).
///
/// ### Variantes de compilación (parámetros `EXTRA_DEFINES`)
///
/// La demo 107 es el *showcase* del corkscrew y está pensada para crecer
/// hasta DPF como `demos/106_tile_field_showcase` pero sin el modelo circular.
/// Los parámetros siguen el mismo patrón que 102/104/106 para que
/// `tools/test-regression.sh` pueda barrerlos sin tocar el fuente:
///
/// ```text
/// EXTRA_DEFINES="-DK_TILE_WIDTH=16 -DK_PLANES=4 -DK_FETCH_MODE=0 -DK_DUAL=0"
///   K_TILE_WIDTH : 16 | 32           ancho de tile (múltiplo de 16)
///   K_PLANES     : 3 | 4 | 5 | 6      profundidad total (3=8c, 4=16c, 5=32c, 6=EHB/DPF 3+3 ó 3+2)
///   K_TILE_SIZE  : 16 | 32            alto de tile (futuro, para 16×32 / 32×32)
///   K_FETCH_MODE : 0 | 1 | 2 | 3      0=viewport+32 16px DDF $30 offset 0 mod 2
///                                   1=viewport+64 32px DDF $28 offset 16 mod 4 (BPL32)
///                                   2=viewport+64 32px DDF $28 offset 16 mod 4 (BPAGEM)
///                                   3=viewport+64 64px DDF $18 offset 48 mod 8 (BPL32+BPAGEM)
///   K_DUAL       : 0 | 1              0=single playfield, 1=dual 3+3 interleaved (futuro)
///   K_FG_PLANES  : 3                  planos del primer plano en dual (PF1)
///   K_BG_PLANES  : 3                  planos del fondo en dual (PF2)
///   K_SCROLL_X/Y : 0 | 1              ejes activos; X-Limited puro es X=1 Y=0, XY usa xylimited
///   K_MAP_W/H    : 256 | 128 ...      tamaño lógico del mapa en tiles (para altura extra)
///   K_WRAP_X/Y   : 0 | 1              1=mapa circular (wrap), 0=borde con edge_tile
///   K_PALETTE    : 0 | 1              0=demo::kPalette 32c, 1=paleta custom de 64c para 6 planos
///   K_VIEWPORT_W/H : 320/256 | 288/224 ... ventana visible
///   K_SCREENS_X/Y  : 16 | 8 ...       pantallas virtuales en X/Y (map = screens*viewport/tile)
/// ```
///
/// DPF 8-way (futuro, como en 102/104): dos `XLimitedPlayfield` con `scroll_y=true`
/// (corkscrew por playfield), PF1 en planos impares (1,3,5) y PF2 en pares
/// (2,4,6), cada uno con su BlocksBitmap interleaved y su `frontbuffer`, pero
/// compartiendo `bitmap_height`/`display_height` y `BPLMOD`. El compositor dual
/// programará `BPLCON0` con DPF=1, `BPLCON2` con prioridad PF1/PF2 y publicará
/// los 6 punteros interleaved más **el split vertical de ambos playfields** en
/// la misma línea (`display_height - display_offset`), de modo que cada playfield
/// conserva su scroll de 8 vías optimizado (1-2 blits/píxel, banda de staging,
/// plane-shift y guarda de 1 word por eje) sin Copper segmentado horizontal.
///
/// Ejemplos:
///
/// ```bash
/// # viewport 320×256 normal (caso base, sin Copper segmentado) — single 4 planos
/// EXTRA_DEFINES="-DK_TILE_WIDTH=16" AMIGA_BIN_PATH=".../bin/win32" \
///   bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean
/// bash demos/107_xlimited_corkscrew/analyze-sequence.sh --warp
///
/// # viewport 288×224 (18×14 tiles) con 16×16 pantallas → mapa 288×224 tiles
/// EXTRA_DEFINES="-DK_VIEWPORT_W=288 -DK_VIEWPORT_H=224 -DK_SCREENS_X=16 -DK_SCREENS_Y=16" ...
///
/// # 384 px con tiles de 32 y fetch normal (viewport 320)
/// EXTRA_DEFINES="-DK_TILE_WIDTH=32" ...
///
/// # 384 px fetch ancho 32 px (DDF $28, offset 16) — necesita viewport+64
/// EXTRA_DEFINES="-DK_TILE_WIDTH=32 -DK_FETCH_MODE=1" ...
///
/// # Futuro DPF 3+3 (cuando se añada K_DUAL=1):
/// EXTRA_DEFINES="-DK_DUAL=1 -DK_TILE_WIDTH=16 -DK_PLANES=6" ...
/// ```
///
/// ### Variantes de ejecución / verificación
///
/// ```bash
/// bash demos/107_xlimited_corkscrew/analyze-sequence.sh --warp
/// # sin --warp para evaluar suavidad a 50 fps (warp=false por defecto en run-demo)
/// bash ./tools/test-regression.sh --demo demos/107_xlimited_corkscrew --warp
/// # Barrido de parámetros (cuando DPF esté implementado):
/// bash ./tools/test-regression.sh --demo demos/107_xlimited_corkscrew --warp --keep-going
/// ```
///
/// Si `analyze-sequence.sh --warp` informa `FAILED detail=0x10704` (67332),
/// es el fill inicial desbordando el `FramePlan` (cols*rows jobs >128): el
/// `XLimitedPlayfield::fill_screen` es atómico; la demo debe rellenar en lotes
/// fila a fila ejecutando el plan cuando se llena (ver `DemoGame::init`).
/// El canal lateral `dist/tools/run/run-demo.js:701` propaga ese `detail`
/// como `Error: La demo informó FAILED por canal lateral: detail=67332`.
/// Tras la corrección, el umbral de telemetría videoposx/mapposx es 64 para
/// tolerar el muestreo cada 20 ms con warp y el wrap de 8 bits tras 600 frames.
///
struct XlimitedConfig {
    TileLayerMap map {};
    const u16* tileset = nullptr;      // banco de bloques (BlocksBitmap->Planes[0])
    u16 tileset_count = 0;
    u8 planes = 4;                     // BLOCKSDEPTH, rango 3..6 (3=8c, 4=16c, 5=32c, 6=EHB/DPF 3+3)
    u16 tile_width = 16;               // múltiplo de 16 (16 ó 32 para la demo)
    u16 tile_height = 16;              // BLOCKHEIGHT; BLOCKPLANELINES = tile_height*planes (48/64/80/96)
    u16 bitmap_width = 0;              // 0=auto: viewport_w + EXTRAWIDTH (32 ó 64 según fetch_mode); si !=0 se respeta
    u8 fetch_mode = 0;                 // 0=normal 16px, 1=BPL32, 2=BPAGEM, 3=BPL32+BPAGEM
    u16 viewport_w = xlimited_detail::kScreenW; // ancho visible (320 por defecto, 288 alternativo)
    u16 viewport_h = xlimited_detail::kScreenH; // alto visible (256 por defecto, 224 alternativo)
    u8 screens_x = 16;                 // pantallas virtuales en X (map_w = screens_x * viewport_w/tile_width)
    u8 screens_y = 16;                 // pantallas virtuales en Y (map_h = screens_y * viewport_h/tile_height)
    bool scroll_y = false;             // true = corkscrew/XY: display_height = viewport_h + 2*tile_height,
                                       // banda de staging, fill de display_blocks_per_col y split vertical
    ScrollMode scroll_mode = ScrollMode::EightWay; // especialización del scroll (deriva scroll_y)
    u8 max_step = 1;               // px/frame máximos por eje de AVANCE (salto).
                                   // El algoritmo pinta cada sub-paso de 1 px ANTES de
                                   // avanzar videoposx → nunca revela píxeles sin pintar.
                                   // Un salto de N px equivale a N sub-pasos atómicos
                                   // (N columnas de tiles pintadas; coste de Blitter ∝ N).
    bool linear_display = false;       // true = display LINEAL sin split: el bitmap duplica el bucle
                                       // (espejo de filas) y el wrap se lee de forma contigua. Elimina la
                                       // limitación del split en raster 256..296 (comparador de 8 bits)
                                       // a costa de duplicar cada blit (dibujo + espejo).
};

/// Campo XLimited: scroll infinito en X con bitmap interleaved y wrap vertical.
///
/// Es una ESPECIALIZACIÓN de `Playfield`: implementa el mapeo lógico→físico del
/// corkscrew (planelínea del bucle, walk horizontal, espejo del modo lineal) y
/// la variante de scroll (8-way, X-only según `scroll_y`). Las primitivas de
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
template <ScrollConsts SC = ScrollConsts{}>
class XLimitedPlayfield : public Playfield {
public:
    XLimitedPlayfield() = default;

    // No copiable (posee memoria Chip)
    XLimitedPlayfield(const XLimitedPlayfield&) = delete;
    XLimitedPlayfield& operator=(const XLimitedPlayfield&) = delete;

    /// Fila (en planelíneas) de inicio de la fila de mundo `wy` en el bucle
    /// vertical (costura del split). Hook del mapeo de la base `Playfield`.
    u32 planeline_for(s32 wy) const override {
        return static_cast<u32>(dmod2(wy)) * m_cfg.planes;
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
        if (!m_initialized) return false;
        m_dbg_ink_visible = 0; // DEBUG: reinicio el flag del frame (hipótesis offset)
        const s32 lim = m_max_step;
        if (dx > lim) dx = lim; else if (dx < -lim) dx = -lim;
        if (dy > lim) dy = lim; else if (dy < -lim) dy = -lim;
        if (dx > 0) for (s32 i = 0; i < dx; ++i) { if (!m_scroll.scroll_right(plan, *this)) return false; }
        else if (dx < 0) for (s32 i = 0; i < -dx; ++i) { if (!m_scroll.scroll_left(plan, *this)) return false; }
        if (dy > 0) for (s32 i = 0; i < dy; ++i) { if (!m_scroll.scroll_down(plan, *this)) return false; }
        else if (dy < 0) for (s32 i = 0; i < -dy; ++i) { if (!m_scroll.scroll_up(plan, *this)) return false; }
        return true;
    }

    /// Salto máximo (px/frame por eje) configurable. 1 = comportamiento 1px clásico.
    constexpr void set_scroll_step(u8 v) { m_max_step = v; }
    constexpr u8 scroll_step() const { return m_max_step; }

    /// DEBUG: ¿el frame pintó algún bloque de relleno dentro de la zona visible?
    constexpr u8 dbg_ink_visible() const { return m_dbg_ink_visible; }
    /// DEBUG: fila del bucle (0..display_height) del último ink visible.
    constexpr u8 dbg_ink_visible_row() const { return m_dbg_ink_visible_row; }

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
    /// \param viewport_h   alto visible (cfg.viewport_h)
    /// \param tile_height  alto de bloque (cfg.tile_height, 16)
    /// \param scroll_y     true = corkscrew (XY), false = X-only
    /// \param map_width_blocks  ancho del mapa en bloques (ej. screens_x*viewport_w/tile_width)
    /// \param bitmap_blocks_per_row  BITMAPWIDTH / tile_width
    /// \param planes  profundidad (4)
    /// \return altura en píxeles (viewport_h + EXTRAHEIGHT? + extra +1+3)
    static constexpr u16 compute_bitmap_height(u16 viewport_h,
                                               u16 tile_height,
                                               bool scroll_y,
                                               u16 map_width_blocks,
                                               u16 bitmap_blocks_per_row,
                                               u8 planes) {
        const u16 display_h = static_cast<u16>(
            viewport_h + (scroll_y ? static_cast<u16>(2u * tile_height) : 0));
        u16 h = static_cast<u16>(
            display_h + (map_width_blocks / bitmap_blocks_per_row / planes) + 1 + 3);
        if (scroll_y) {
            h = static_cast<u16>(((h + tile_height - 1u) / tile_height) * tile_height);
        }
        return h;
    }

    /// Reserva el bitmap interleaved y prepara el estado inicial.
    ///
    /// Reserva `bitmap_bytes_per_row * bitmap_height * planes` bytes en Chip
    /// RAM con alineación 16 (como `AllocBitMap(..., BMF_INTERLEAVED|BMF_CLEAR)`).
    /// `frontbuffer` apunta a `base + bitmapoffset` para los modos de fetch
    /// ancho (16 bytes para BPL32, 48 para 4x). En modo normal offset=0.
    bool begin(MemorySystem& memory, const XlimitedConfig& cfg) {
        m_cfg = cfg;
        m_max_step = m_cfg.max_step ? m_cfg.max_step : 1; // salto configurable (≥1)
        // Especialización del scroll: HorizontalOnly no usa banda de staging ni
        // split (display_height = viewport_h, X-only). Los demás modos conservan
        // el valor de scroll_y del config.
        if (m_cfg.scroll_mode == ScrollMode::HorizontalOnly) m_cfg.scroll_y = false;
        if (!valid_config()) return false;

        // Derivar bitmap_width si es 0: viewport_w + EXTRAWIDTH según fetch_mode.
        if (m_cfg.bitmap_width == 0) {
            const u16 extra = (m_cfg.fetch_mode == 0) ? xlimited_detail::kExtraW32 : xlimited_detail::kExtraW64;
            m_cfg.bitmap_width = static_cast<u16>(m_cfg.viewport_w + extra);
        }
        m_bitmap_width = m_cfg.bitmap_width;
        m_bytes_per_row = static_cast<u16>(m_bitmap_width / 8u);
        m_bitmap_blocks_per_row = static_cast<u16>(m_bitmap_width / m_cfg.tile_width);
        m_block_planes_lines = static_cast<u16>(m_cfg.tile_height * m_cfg.planes);
        // Derivar map_w/h si no vienen dados: screens_x/y * (viewport/tile)
        const u16 derived_map_w = static_cast<u16>(m_cfg.screens_x * (m_cfg.viewport_w / m_cfg.tile_width));
        const u16 derived_map_h = static_cast<u16>(m_cfg.screens_y * (m_cfg.viewport_h / m_cfg.tile_height));
        const u16 map_w_blocks = m_cfg.map.width ? m_cfg.map.width
                               : (m_cfg.map.wrap_x ? static_cast<u16>(m_cfg.map.wrap_x) : derived_map_w);
        // map_h no afecta a bitmap_height en X-Limited puro, pero se valida para scroll_y
        (void)derived_map_h;
        m_bitmap_height = compute_bitmap_height(
            m_cfg.viewport_h, m_cfg.tile_height, m_cfg.scroll_y,
            map_w_blocks,
            m_bitmap_blocks_per_row, m_cfg.planes);
        // Altura mínima: max(viewport_h+1+3, 16*tile_height) para que los 16 valores de mapy (0..15) quepan.
        // Para viewport 224×16 con tile 16 → 224+4=228 <256, se eleva a 256 para acomodar y=15*BLOCKPLANELINES.
        const u16 min_h_viewport = static_cast<u16>(m_cfg.viewport_h + 1 + 3);
        const u16 min_h_blocks = static_cast<u16>(16u * m_cfg.tile_height);
        const u16 min_h = (min_h_viewport > min_h_blocks) ? min_h_viewport : min_h_blocks;
        if (m_bitmap_height < min_h) m_bitmap_height = min_h;

        // Bucle vertical del display (corkscrew): viewport_h + EXTRAHEIGHT
        // (2 bloques de banda de staging). En X-only coincide con viewport_h.
        m_display_height = static_cast<u16>(
            m_cfg.viewport_h + (m_cfg.scroll_y ? static_cast<u16>(2u * m_cfg.tile_height) : 0));
        m_display_planelines = static_cast<u16>(m_display_height * m_cfg.planes);
        // Guard de `ScrollConsts` (NTTP): si la escena instancia este playfield
        // con constantes a priori, deben coincidir con el cfg; si no, el
        // ScrollEngine usaría geometría equivocada (desincronización silenciosa).
        if (SC.tile_width != 0u && SC.tile_width != m_cfg.tile_width) return false;
        if (SC.tile_height != 0u && SC.tile_height != m_cfg.tile_height) return false;
        if (SC.planes != 0u && SC.planes != m_cfg.planes) return false;
        if (SC.display_height != 0u && SC.display_height != m_display_height) return false;
        if (SC.display_planelines != 0u && SC.display_planelines != m_display_planelines) return false;
        // BITMAPBLOCKSPERCOL del corkscrew: filas de bloque del bucle vertical.
        m_bitmap_blocks_per_col = static_cast<u16>(m_display_height / m_cfg.tile_height);

        // Modo display lineal (sin split): se añade un ESpejo del bucle (filas
        // display_height..2*display_height = copia de 0..display_height). El
        // display lee de forma contigua display_offset..display_offset+viewport_h
        // y, al cruzar el final del bucle, continúa por el espejo (que se mantiene
        // en sincronía con cada blit). Esto elimina el split vertical y su
        // limitación del comparador de 8 bits (raster 256..296).
        m_linear_display = m_cfg.linear_display;
        m_mirror_planelines = static_cast<u32>(m_display_height) * m_cfg.planes;
        if (m_linear_display) {
            m_bitmap_height = static_cast<u16>(m_bitmap_height + m_display_height);
        }

// BMF_INTERLEAVED-like: la memoria la posee un `Bitmap` (capa de
        // memoria del engine). Modela la guardia de +64 y el offset de fetch
        // ancho (0/16/48 para BPL32/4x) que el corkscrew necesita: el bloque
        // reservado mide total_bytes+64 y `frontbuffer` = base + offset.
        gfx::BitmapConfig bc;
        bc.width = m_bitmap_width;
        bc.height = m_bitmap_height;
        bc.planes = m_cfg.planes;
        bc.row_bytes = m_bytes_per_row;
        bc.layout = gfx::PlaneLayout::Interleaved;
        bc.alignment = 16;
        bc.domain = gfx::MemoryDomain::Chip;
        bc.frontbase_offset = fetch_bitmap_offset(m_cfg.fetch_mode);
        bc.guard_bytes = 64u;
        if (!m_bitmap.init(memory, bc)) return false;
        m_total_bytes = m_bitmap.total_bytes();
        m_real_base = m_bitmap.allocation_start(); // base del bloque (BPLxPT)
        m_frontbuffer = m_bitmap.bytes().data();   // vía cruda interna (núcleo)

        // BPLMODs: BITMAPBYTESPERROW*planes - SCREENBYTESPERROW - modulo_offset
        // modulo_offset = 2 (normal), 4 (BPL32/BPAGEM), 8 (BPL32+BPAGEM) según fetch_mode
        // SCREENBYTESPERROW = cfg.viewport_w / 8
        const u16 modulo_offset = fetch_modulo_offset(m_cfg.fetch_mode);
        const s32 mod = static_cast<s32>(m_bytes_per_row) * m_cfg.planes -
                        (m_cfg.viewport_w / 8) - modulo_offset;
        m_bpl1mod = static_cast<u16>(mod);
        m_bpl2mod = static_cast<u16>(mod);

        m_scroll.state().mapposx = 0;
        m_scroll.state().videoposx = 0;
        m_scroll.state().mapposy = 0;
        m_scroll.state().videoposy = 0;
m_scroll.state().previous_xdirection = 0; // DIRECTION_IGNORE (0=ignore, 1=left, 2=right)
        m_savewordpointer = nullptr;
        m_saveword = 0;
        m_blocks_buffer = reinterpret_cast<const u8*>(m_cfg.tileset);
        // Sincronizar los campos de la base `Playfield` (usados por las
        // primitivas CPU y los getters de geometría).
        m_planes = m_cfg.planes;
        m_width = m_bitmap_width;
        m_height = m_bitmap_height;
        m_initialized = true;
        return true;
    }

    /// Rellena la pantalla inicial (equivalente a FillScreen).
    ///
    /// Emite `BITMAPBLOCKSPERROW * colHeight` jobs de Blitter en el
    /// `FramePlan` dado, donde colHeight = visibleRows + (scroll_y ? 2 : 0)
    /// (el corkscrew pre-rellena el bucle vertical completo de display,
    /// viewport_h + EXTRAHEIGHT, para que la banda de staging de 2 bloques
    /// tenga contenido coherente desde el primer frame).
    bool fill_screen(graphics::FramePlan& plan) const {
        if (!m_initialized) return false;
        const u16 cols = m_bitmap_blocks_per_row;
        const u16 visibleRows = static_cast<u16>(m_cfg.viewport_h / m_cfg.tile_height);
        const u16 colHeight = m_cfg.scroll_y ? m_bitmap_blocks_per_col : visibleRows;
        const u16 rows = colHeight;
        for (u16 b = 0; b < rows; ++b) {
            for (u16 a = 0; a < cols; ++a) {
                if (m_cfg.map.tile_at(a, b) == m_cfg.map.empty_tile) continue; // vacío: no pintar
                const u16 x = a * m_cfg.tile_width;
                const u16 y = b * m_block_planes_lines; // planeline
                const u16 mapx = a;
                const u16 mapy = b;
                auto job = draw_block_job(x, y, mapx, mapy);
                if (!plan.add_tile_block_copy(job)) return false;
                if (m_linear_display) {
                    const u16 ym = static_cast<u16>(static_cast<u32>(y) + m_mirror_planelines);
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
        const u32 dst_offset = static_cast<u32>(y) * m_bytes_per_row + x_word;

        // Resolución del bloque del mapa (wrapping si el mapa es circular)
        const u16 block = m_cfg.map.tile_at(mapx, mapy);
        // Layout del banco de bloques: BLOCKSWIDTH/BLOCKWIDTH del original
        // Para tile_width !=16, el número de words por fila escala.
        const u16 blocks_per_row_src = 20; // BLOCKSWIDTH/BLOCKWIDTH del original (320/16)
        const u16 src_bytes_per_row = 40;  // BLOCKSWIDTH/8 (320/8)
        const u16 words_per_block = static_cast<u16>(m_cfg.tile_width / xlimited_detail::kBlock);
        const u32 src_row = static_cast<u32>(block / blocks_per_row_src) *
                            static_cast<u32>(m_block_planes_lines) * src_bytes_per_row;
        const u32 src_col = static_cast<u32>(block % blocks_per_row_src) *
                            static_cast<u32>(words_per_block * 2u);
        const u32 src_offset = src_row + src_col;

        const u8* src = m_blocks_buffer ? m_blocks_buffer + src_offset : nullptr;
        // Destino interleaved: frontbuffer + y*BITMAPBYTESPERROW + x_word
        u16* dst = reinterpret_cast<u16*>(
            const_cast<u8*>(m_frontbuffer) + dst_offset);

        // Un único blit de BLOCKPLANELINES líneas y words_per_block words.
        // El Blitter ve el bitmap interleaved como una sola columna tall.
        // bltsize = BLOCKPLANELINES*64 + words
        const u16 words = words_per_block;
        // Módulos del Blitter (ver §6)
        const s16 src_mod = static_cast<s16>(src_bytes_per_row - words * 2);
        const s16 dst_mod = static_cast<s16>(m_bytes_per_row - words * 2);

        // Para que FramePlan::add_tile_block_copy valide, los strides deben
        // ser no nulos. En modo interleaved el stride real es el módulo, pero
        // el validador exige plane_stride_bytes !=0. Usamos 2 como placeholder
        // inocuo porque bitplane_count=1 sólo itera una vez.
        return {
            graphics::BlitJobKind::TileBlockCopy,
            nullptr,
            reinterpret_cast<const u16*>(src),
            dst,
            words,
            m_block_planes_lines,
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

    constexpr u16 twoblockstep() const {
        // TWOBLOCKS del corkscrew (XYLimited): BITMAPBLOCKSPERROW - NUMSTEPS_Y
        //   x*2 + (tile_height - x) = bitmap_blocks_per_row  →  x = bpr - TH
        return static_cast<u16>(m_bitmap_blocks_per_row > m_cfg.tile_height
            ? m_bitmap_blocks_per_row - m_cfg.tile_height : 0);
    }
    constexpr u16 block_videoposy() const {
        // Banda de staging: SIEMPRE dentro del bucle de display (0..display_height),
        // nunca en las filas extra (display_height..bitmap_height) que usa el
        // planeaddx walk horizontal. El original envuelve en bitmap_height; ese
        // wrap hacía que cada bitmap_height px la fila entrante se dibujara en
        // las filas extra que el display SÍ muestra al scrollear en X (tile
        // visible en el área de pantalla y banda de staging sin refrescar).
        // Corregido 2026-08-31: envolver en display_height mantiene la fila
        // entrante siempre en la banda oculta de 32 filas.
        return static_cast<u16>(
            (m_scroll.state().mapposy / m_cfg.tile_height * m_cfg.tile_height) % m_display_height);
    }
    /// Añade el blit de un bloque y, en modo lineal (espejo), también el espejo.
    /// Devuelve false si el plan no admite el/los job(s).
    bool add_draw(graphics::FramePlan& plan, u16 x, u16 y, u16 mapx, u16 mapy) {
        // Tile 'vacío' (empty_tile): NO se pinta (no consume slot de Blitter).
        if (m_cfg.map.tile_at(mapx, mapy) == m_cfg.map.empty_tile) return true;
        // DEBUG (hipótesis viewport-offset): ¿este bloque de relleno cae en las
        // filas del bucle que el display está mostrando AHORA mismo? Si sí, el
        // indice del viewport respecto al framebuffer hace visibles los tiles.
        {
            const s32 vps = m_scroll.state().videoposy;
            const u32 d = static_cast<u32>(dmod1(static_cast<u32>(vps) + m_cfg.tile_height));
            const u32 row = static_cast<u32>(y) / m_cfg.planes; // fila real del bucle
            const u32 rel = (row + m_display_height - (d % m_display_height)) % m_display_height;
            // Horiz. visible en el framebuffer (ventana que lee el chip a partir de
            // ROUND2(videoposx)): ignorar la columna derecha (x ≈ x0+bitmap_width).
            const u32 x0v = static_cast<u32>(m_scroll.state().videoposx & ~(m_cfg.tile_width - 1)) % m_bitmap_width;
            const u32 xb = static_cast<u32>(x) % m_bitmap_width;
            const bool xvis = (xb < x0v + m_cfg.viewport_w) && (xb + m_cfg.tile_width > x0v) ||
                              (x0v + m_cfg.viewport_w > m_bitmap_width && xb < (x0v + m_cfg.viewport_w) % m_bitmap_width);
            if (rel < m_cfg.viewport_h && xvis) { m_dbg_ink_visible = 1; m_dbg_ink_visible_row = static_cast<u8>(row); }
        }
        if (!plan.add_tile_block_copy(draw_block_job(x, y, mapx, mapy))) return false;
        if (m_linear_display) {
            // Espejo: mismo bloque en planelínea y + m_mirror_planelines (copia del bucle).
            const u16 ym = static_cast<u16>(static_cast<u32>(y) + m_mirror_planelines);
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
        const s16 dst_mod = static_cast<s16>(m_bytes_per_row * planes - words * 2);
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = src + static_cast<u32>(p) * (src_plane_stride / 2u);
            u16* d = reinterpret_cast<u16*>(const_cast<u8*>(m_frontbuffer) +
                (planeline_start + static_cast<u32>(p)) * m_bytes_per_row + x_byte);
            graphics::BlitJob job {
                graphics::BlitJobKind::CopyRect, nullptr, s, d,
                words, seg_rows, src_mod, dst_mod,
                1, 0, src_plane_stride, static_cast<u32>(m_bytes_per_row * planes), false
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
                          u16 src_row_bytes, u32 src_plane_stride, u8 planes) override {
        if (!m_initialized || src.empty() || planes == 0) return false;
        if (wx < 0 || (wx & 15) != 0) return false;
        const s32 loop = dmod2(wy);
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
                (r + remaining > m_display_height) ? (m_display_height - r) : remaining);
            // Dibujo en el bucle (partido por la costura si cruza display_height).
            if (!emit_world_rect(plan, sbase, x_byte, static_cast<u32>(r) * planes,
                    words, seg, src_row_bytes, src_plane_stride, planes)) return false;
            // En modo lineal, duplicar al espejo para que el framebuffer quede coherente.
            if (m_linear_display) {
                if (!emit_world_rect(plan, sbase, x_byte,
                        static_cast<u32>(r + m_display_height) * planes,
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
    /// `source_modulo_bytes` para el canal A=masks, ver amiga_minimal.cpp).
    bool emit_world_rect_masked(graphics::FramePlan& plan, const u16* src, const u16* mask,
                                u16 x_byte, u32 planeline_start, u16 words, u16 seg_rows,
                                u16 src_row_bytes, u32 src_plane_stride, u8 planes) {
        const s16 src_mod = static_cast<s16>(src_row_bytes - words * 2);
        const s16 dst_mod = static_cast<s16>(m_bytes_per_row * planes - words * 2);
        for (u8 p = 0; p < planes; ++p) {
            const u16* s = src + static_cast<u32>(p) * (src_plane_stride / 2u);
            u16* d = reinterpret_cast<u16*>(const_cast<u8*>(m_frontbuffer) +
                (planeline_start + static_cast<u32>(p)) * m_bytes_per_row + x_byte);
            graphics::BlitJob job {
                graphics::BlitJobKind::MaskedBobCookieCut, mask, s, d,
                words, seg_rows, src_mod, dst_mod,
                1, 0, src_plane_stride, static_cast<u32>(m_bytes_per_row * planes), false
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
                                 u8 planes) override {
        if (!m_initialized || src.empty() || mask.empty() || planes == 0) return false;
        if (wx < 0 || (wx & 15) != 0) return false;
        const s32 loop = dmod2(wy);
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
                (r + remaining > m_display_height) ? (m_display_height - r) : remaining);
            if (!emit_world_rect_masked(plan, sbase, mbase, x_byte, static_cast<u32>(r) * planes,
                    words, seg, src_row_bytes, src_plane_stride, planes)) return false;
            if (m_linear_display) {
                if (!emit_world_rect_masked(plan, sbase, mbase, x_byte,
                        static_cast<u32>(r + m_display_height) * planes,
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
            const_cast<u8*>(m_frontbuffer) + byte_offset);
        m_saveword = *m_savewordpointer;
    }
    void restore_saveword() {
        if (m_savewordpointer) *m_savewordpointer = m_saveword;
    }

    // --- ScrollSink: el algoritmo vive en `ScrollEngine` (scroll_engine.hpp);
    // estos cuatro métodos delegan en él; el playfield solo aporta el layout.

    /// Scroll de 1 px a la derecha (plane-shifted) — ScrollRight corkscrew.
    bool scroll_right(graphics::FramePlan& plan) {
        if (!m_initialized) return false;
        return m_scroll.scroll_right(plan, *this);
    }
    /// Scroll de 1 px a la izquierda (no plane-shifted) — ScrollLeft corkscrew.
    bool scroll_left(graphics::FramePlan& plan) {
        if (!m_initialized) return false;
        return m_scroll.scroll_left(plan, *this);
    }
    /// Scroll vertical 1 px hacia abajo — ScrollDown corkscrew.
    bool scroll_down(graphics::FramePlan& plan) {
        if (!m_initialized) return false;
        return m_scroll.scroll_down(plan, *this);
    }
    /// Scroll vertical 1 px hacia arriba — ScrollUp corkscrew.
    bool scroll_up(graphics::FramePlan& plan) {
        if (!m_initialized) return false;
        return m_scroll.scroll_up(plan, *this);
    }

    /// Vista de hardware para el compositor (planeaddx + BPLCON1 + offset Y).
    PlayfieldHardwareView hardware_view() const override {
        PlayfieldHardwareView v {};
        v.bitplanes = m_frontbuffer;
        v.real_base = m_real_base;
        v.bitmap_bytes_per_row = m_bytes_per_row;
        v.planes = m_cfg.planes;
        v.bitmap_height = m_bitmap_height;
        v.viewport_w = m_cfg.viewport_w;
        v.viewport_h = m_cfg.viewport_h;
        v.videoposx = m_scroll.state().videoposx;
        v.mapposx = m_scroll.state().mapposx;
        v.videoposy = m_scroll.state().videoposy;
        v.mapposy = m_scroll.state().mapposy;

        // Cálculo idéntico a UpdateCopperlist (xlimited.c:579-613) para
        // fetch normal (I=tile_width). Para fetch ancho se generaliza con I.
const u16 I = fetch_scroll_pixels(m_cfg.fetch_mode);
        const s32 xpos = m_scroll.state().videoposx + static_cast<s32>(I) - 1;
        const u32 planeaddx = static_cast<u32>(xpos / I) * (I / 8u);
        s32 fine = (static_cast<s32>(I) - 1) - (xpos & (I - 1));
        u16 scroll = static_cast<u16>((fine & 15) * 0x11);
        if (fine & 16) scroll |= 0x4400;
        if (fine & 32) scroll |= 0x8800;
        v.planeaddx = planeaddx;
        v.bplcon1 = scroll;
        v.bpl1mod = m_bpl1mod;
        v.bpl2mod = m_bpl2mod;
        // Offset vertical del display (corkscrew, UpdateCopperlist xlimited.c):
        //   yoffset = (videoposy + BLOCKHEIGHT) % BITMAPHEIGHT
        // el display empieza un bloque por debajo de videoposy, dejando la
        // banda de staging (2 bloques) por encima de la ventana visible.
        // En X-only (scroll_y=false) videoposy es 0 y no hay offset ni split.
        u16 display_offset = 0;
        if (m_cfg.scroll_y) {
const u16 vy = static_cast<u16>(dmod2(m_scroll.state().videoposy));
        display_offset = static_cast<u16>(dmod1(static_cast<u32>(vy) + m_cfg.tile_height));
        }
        v.display_height = m_display_height;
        v.display_offset = display_offset;
        v.planeaddy = static_cast<u32>(display_offset) * m_cfg.planes * m_bytes_per_row;
        // Split vertical: la vuelta al inicio del bucle ocurre a
        // `display_height - display_offset` filas dentro de la ventana. Solo se
        // necesita si esa vuelta cae dentro del viewport (yoffset + VH > DH).
        v.split_line = static_cast<u16>(m_display_height - display_offset);
        // En modo lineal no hay split: el wrap lo resuelve el espejo.
        v.split_active = !m_linear_display && m_cfg.scroll_y && v.split_line < m_cfg.viewport_h;
        v.split_planeaddy = 0; // fila 0 (los punteros del split solo suman planeaddx)
        // plane_bytes para validación: bytes totales
        v.plane_bytes = static_cast<u32>(m_bytes_per_row * m_bitmap_height * m_cfg.planes);
        return v;
    }

    // Accesores para verificación y demo
    constexpr s32 mapposx() const override { return m_scroll.state().mapposx; }
    constexpr s32 videoposx() const override { return m_scroll.state().videoposx; }
    constexpr s32 mapposy() const override { return m_scroll.state().mapposy; }
    constexpr s32 videoposy() const override { return m_scroll.state().videoposy; }

    /// Columna de mundo de un objeto FIJO en la columna de pantalla `sx`.
    constexpr s32 screen_to_world_x(s16 sx) const { return m_scroll.state().mapposx + sx; }
    /// Fila de mundo de un objeto FIJO en la fila de pantalla `sy`. La ventana
    /// visible NO empieza en videoposy (la banda de staging queda un bloque por
    /// encima): equivale a `(mapposy + tile_height + sy) % display_height`.
    constexpr s32 screen_to_world_y(s16 sy) const { return screen_to_bitmap_row(sy); }

    /// Fila (en píxeles) del bucle vertical donde empieza la ventana visible.
    /// Coincide con `(videoposy + tile_height) % display_height`.
    constexpr s32 display_offset() const {
        return static_cast<s32>(dmod1(static_cast<u32>(m_scroll.state().videoposy) + m_cfg.tile_height));
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
        return static_cast<u16>(m_cfg.viewport_h + 40u) <= 255u;
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
        return m_linear_display ? row : dmod2(row);
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
    constexpr u16 tile_width() const { return m_cfg.tile_width; }
    constexpr u16 tile_height() const { return m_cfg.tile_height; }
    constexpr u16 viewport_w() const { return m_cfg.viewport_w; }
    constexpr u16 viewport_h() const { return m_cfg.viewport_h; }
    constexpr u16 display_planelines() const { return m_display_planelines; }
    constexpr u16 bytes_per_row() const { return m_bytes_per_row; }
    constexpr u16 bitmap_blocks_per_col() const { return m_bitmap_blocks_per_col; }
    constexpr u16 map_width_blocks() const {
        return m_cfg.map.width ? m_cfg.map.width
            : static_cast<u16>(m_cfg.screens_x * (m_cfg.viewport_w / m_cfg.tile_width));
    }
    constexpr u16 map_height_blocks() const {
        return m_cfg.map.height ? m_cfg.map.height
            : static_cast<u16>(m_cfg.screens_y * (m_cfg.viewport_h / m_cfg.tile_height));
    }
    constexpr u16 map_wrap_x() const { return m_cfg.map.wrap_x; }
    constexpr u16 map_wrap_y() const { return m_cfg.map.wrap_y; }
    constexpr bool one_direction() const { return m_cfg.scroll_mode == ScrollMode::OneDirection; }
constexpr u16 block_planes_lines() const { return m_block_planes_lines; }
    constexpr bool initialized() const { return m_initialized; }
    constexpr u16 bpl1mod() const { return m_bpl1mod; }

    /// Reinicia el scroll a 0 sin re-reservar Chip RAM (para demo infinita).
    void reset_scroll() {
        m_scroll.state().mapposx = 0;
        m_scroll.state().videoposx = 0;
        m_scroll.state().mapposy = 0;
        m_scroll.state().videoposy = 0;
        m_scroll.state().previous_xdirection = 0;
        m_savewordpointer = nullptr;
        m_saveword = 0;
    }

private:
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
        if (m_cfg.map.cells.empty() && m_cfg.map.width == 0 && m_cfg.screens_x == 0) return false;
        return true;
    }

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

    XlimitedConfig m_cfg {};
    gfx::Bitmap m_bitmap {};   // capa de memoria (posee el bloque Chip)
    u8* m_real_base = nullptr;
    const u8* m_blocks_buffer = nullptr;
    u16 m_bitmap_width = xlimited_detail::kBitmapW32;
    u16 m_bitmap_blocks_per_row = xlimited_detail::kBlocksPerRow32;
    u16 m_block_planes_lines = 0; // recalculado en begin(): BLOCKHEIGHT*planes
    u16 m_bitmap_height = 0; // recalculado en begin(): compute_bitmap_height(viewport_h, mapW, blocksPerRow, planes)
    u16 m_display_height = 0; // bucle vertical del display = viewport_h + 2*tile_height (corkscrew)
    u16 m_display_planelines = 0; // display_height * planes (modulus del split)
    u16 m_bitmap_blocks_per_col = 0; // BITMAPBLOCKSPERCOL = display_height / tile_height
    bool m_linear_display = false;  // display lineal (sin split): espejo del bucle
    u32 m_mirror_planelines = 0;    // desplazamiento del espejo (display_height*planes)
    u16 m_bpl1mod = 0, m_bpl2mod = 0;
    ScrollEngine<XLimitedPlayfield<SC>, SC> m_scroll {}; // cámara + algoritmo (§7)
    u16* m_savewordpointer = nullptr;             // guarda de 1 word plane-shift (sink)
    u16 m_saveword = 0;
    u8 m_max_step = 1;                            // salto máx. px/frame por eje (config)
    u8 m_dbg_ink_visible = 0;                     // DEBUG: ink dentro de la zona visible
    u8 m_dbg_ink_visible_row = 0;                 // DEBUG: fila del bucle donde cayó el ink
};

/// Compositor mínimo para XLimited/corkscrew (single playfield interleaved).
///
/// Es la única capa que toca BPLCON0/BPLCON1/BPLxPT/BPLMOD/DIW/DDF. A
/// diferencia de `DpfDisplayComposer` no gestiona dos playfields; sí emite el
/// **split vertical** del corkscrew (segundo juego de BPLxPT a la fila 0 del
/// bucle de display cuando `view.split_active`). Paleta al inicio del frame y
/// doble buffer de copperlist. Úsalo así:
///
///   XlimitedDisplayComposer comp;
///   comp.init(memory, {palette, 1536, planes});
///   comp.compose(field.hardware_view());
///   comp.install(backend);
///
/// El compositor reutiliza la misma copperlist en ambos bloques en el primer
/// frame y luego parchea sólo BPLCON1 y los punteros (13 words), igual que
/// `TileScrollScene::patch_copper`, para no pagar el coste de re-emitir la
/// paleta cada frame.
class XlimitedDisplayComposer {
public:
    struct Config {
        const u16* palette = nullptr; // 2^planes colores (8/16/32/64 según planes 3..6)
        u32 copper_bytes = 1536;
        u8 planes = 4; // rango 3..6 (3=8c, 4=16c, 5=32c, 6=EHB/DPF 3+3)
        u16 diwstrt = xlimited_detail::kDiwStrt;
        u16 diwstop = xlimited_detail::kDiwStop;
        u16 ddfstrt = xlimited_detail::kDdfStrt;
        u16 ddfstop = xlimited_detail::kDdfStop;
        const graphics::SpriteManager* sprites = nullptr; // opcional
    };

    bool init(MemorySystem& memory, const Config& cfg) {
        m_cfg = cfg;
        m_copper_blocks[0] = memory.chip.allocate(cfg.copper_bytes, 16);
        m_copper_blocks[1] = memory.chip.allocate(cfg.copper_bytes, 16);
        if (!m_copper_blocks[0].valid() || !m_copper_blocks[1].valid() ||
            !cfg.palette) return false;
        m_initialized = true;
        return true;
    }

    bool compose(const PlayfieldHardwareView& view) {
        return compose(view, nullptr);
    }

    /// Zona OVERLAY genérica: un playfield SEPARADO mostrado en la franja inferior
    /// de la ventana (raster `DIWSTRT_y + view.viewport_h` en adelante). Es un
    /// MECANISMO del display (cambiar BPLCON1/BPL1/2MOD/BPLxPT + paleta en un
    /// raster fijo); no es específico de HUD. El HUD como patrón (texto/marcas)
    /// se compone a NIVEL DE ESCENA como un `CanvasPlayfield` + `Surface`, y se
    /// pasa aquí como la zona overlay. La escena mantiene la ventana DIW abierta
    /// a su tamaño total y la zona solo programa la franja en el raster de corte.
    struct OverlayZone {
        PlayfieldHardwareView view;    // playfield del overlay (canvas)
        const u16* palette = nullptr;  // paleta del overlay (0..2^planes-1), opcional
    };

    bool compose(const PlayfieldHardwareView& view, const OverlayZone* hud) {
        if (!m_initialized || !view.bitplanes) return false;
        if (!valid_view(view)) return false;
        if (!m_copper_initialized) {
            if (!emit_full(0, view, hud) || !emit_full(1, view, hud)) return false;
            m_copper_initialized = true;
            m_active = 0;
            return true;
        }
        const u8 inactive = static_cast<u8>(m_active ^ 1u);
        if (!emit_full(inactive, view, hud)) return false;
        m_active = inactive;
        return true;
    }

    template <typename Backend>
    void install(Backend& backend) const {
        if (m_initialized && m_copper_initialized) {
            backend.install_copper_list(
                static_cast<const u16*>(m_copper_blocks[m_active].data));
        }
    }

    constexpr bool ok() const { return m_ok; }
    constexpr u16 copper_words() const { return m_copper_words; }
    /// Depuración: puntero al bloque de copper activo.
    const u16* debug_active_copper() const {
        return m_copper_initialized ? static_cast<const u16*>(m_copper_blocks[m_active].data) : nullptr;
    }

private:
    static constexpr u16 pointer_high_word(u8 plane) {
        return static_cast<u16>(21u + plane * 4u);
    }
    static constexpr u16 pointer_low_word(u8 plane) {
        return static_cast<u16>(23u + plane * 4u);
    }

    bool valid_view(const PlayfieldHardwareView& v) const {
        if (!v.bitplanes || v.planes == 0 || v.planes > 6) return false;
        if (v.bitmap_bytes_per_row == 0) return false;
        if (v.bitmap_height == 0) return false;
        if (v.display_height == 0 || v.display_offset >= v.display_height) return false;
        if (v.viewport_w == 0 || v.viewport_h == 0) return false;
        // Validación genérica BPLMOD: debe ser bitmap_bytes_per_row*planes - viewport_w/8 - modulo_offset
        // No se valida con literal 320; se comprueba que bpl1mod no excede el total.
        if (v.plane_bytes < static_cast<u32>(v.bitmap_bytes_per_row * v.bitmap_height * v.planes)) return false;
        // Split: la fila del wrap cae dentro de la ventana y no sobrepasa el buffer.
        if (v.split_active && v.split_line >= v.viewport_h) return false;
        return true;
    }

    bool emit_full(u8 block, const PlayfieldHardwareView& view, const OverlayZone* hud = nullptr) {
        copper::Scheduler sched { m_copper_blocks[block] };
        const u16 bplcon0 = static_cast<u16>(
            0x0200u | (static_cast<u16>(view.planes) << 12u));
        sched.move(copper::Register::DMACON,
            static_cast<u16>(copper::DmaSetClear | copper::DmaMaster |
                             copper::DmaCopper | copper::DmaBitplane |
                             (m_cfg.sprites ? m_cfg.sprites->dma_bits() : 0)));
        sched.move(copper::Register::BPLCON0, bplcon0);
        sched.move(copper::Register::BPLCON1, view.bplcon1);
        sched.move(copper::Register::BPLCON2, 0x0000);
        // EHB (HalfBrite): con 6 planos en modo SINGLE, el bit 0 de BPLCON4 activa
        // el modo EHB — el plano 6 deja de ser un bit de color y actúa como
        // selector "half": color = base/2. Es la semántica del tilebank BASES-
        // PRIMERO de la 201 (índices 0..31 base, 32..63 = half automático).
        if (view.planes == 6u) sched.move(copper::Register::BPLCON4, 0x0001u);
        sched.move(copper::Register::BPL1MOD, view.bpl1mod);
        sched.move(copper::Register::BPL2MOD, view.bpl2mod);
        sched.move(copper::Register::DIWSTRT, m_cfg.diwstrt);
        sched.move(copper::Register::DIWSTOP, m_cfg.diwstop);
        sched.move(copper::Register::DDFSTRT, m_cfg.ddfstrt);
        sched.move(copper::Register::DDFSTOP, m_cfg.ddfstop);
        // Paleta primero: si el split está en una línea alta, los MOVEs de color
        // deben aplicar al inicio del frame y no tras el WAIT del split.
        sched.emit_palette(m_cfg.palette);
        for (u8 p = 0; p < view.planes; ++p) {
            const u32 addr = reinterpret_cast<u32>(view.real_base) +
                             view.planeaddx + view.planeaddy +
                             static_cast<u32>(p) * view.bitmap_bytes_per_row;
            // En interleaved, Planes[p] = base + p*BITMAPBYTESPERROW + Y*planes*bytes.
            // planeaddy aporta el offset vertical (display_offset) y planeaddx el horizontal.
            sched.move_bitplane_pointer(p, reinterpret_cast<const void*>(addr));
        }
        // Split vertical del corkscrew: al llegar a `split_line` filas dentro de
        // la ventana, los punteros vuelven a la fila 0 del bucle de display.
        // Limitación OCS: el comparador de WAIT del Copper usa 8 bits con
        // semántica ">=" (vpos&0xFF), por lo que una línea raster>255 (cuando
        // display_offset ∈ [33,73]) no se puede esperar con precisión; el WAIT
        // dispara en la primera coincidencia del byte bajo (línea raster-256,
        // < 256) y el original (XYLimited) también degrada a 255. Se recorta a
        // 255: la banda de 1..41 filas al pie muestra el wrap adelantado
        // (inherente al chipset; ver docs/architecture/AMIGA_8WAY_SCROLLING.md §13).
        u16 raster = 0;
        if (view.split_active) {
            raster = static_cast<u16>((m_cfg.diwstrt >> 8u) + view.split_line);
            const u8 wait = raster > 0xffu ? 0xffu : static_cast<u8>(raster);
            sched.wait_line(wait);
            for (u8 p = 0; p < view.planes; ++p) {
                const u32 addr = reinterpret_cast<u32>(view.real_base) +
                                 view.planeaddx + view.split_planeaddy +
                                 static_cast<u32>(p) * view.bitmap_bytes_per_row;
                sched.move_bitplane_pointer(p, reinterpret_cast<const void*>(addr));
            }
        }
        // El blanking de abajo solo si no estorba con un split en línea alta.
        if (hud != nullptr) {
            // Zona HUD: cambia BPLCON1 (sin scroll), modulos y BPLxPT al playfield
            // del HUD en el raster `DIWSTRT_y + main`. La ventana DIW ya está
            // abierta al total (la escena la configura así con HUD).
            const u16 hud_raster = static_cast<u16>((m_cfg.diwstrt >> 8u) + view.viewport_h);
            sched.wait_line(hud_raster > 0xffu ? 0xffu : static_cast<u8>(hud_raster));
            sched.move(copper::Register::BPLCON1, 0x0000);
            sched.move(copper::Register::BPL1MOD, hud->view.bpl1mod);
            sched.move(copper::Register::BPL2MOD, hud->view.bpl2mod);
            for (u8 p = 0; p < hud->view.planes; ++p) {
                const u32 addr = reinterpret_cast<u32>(hud->view.real_base) +
                                 static_cast<u32>(p) * hud->view.bitmap_bytes_per_row;
                sched.move_bitplane_pointer(p, reinterpret_cast<const void*>(addr));
            }
            if (hud->palette != nullptr) {
                const u8 count = static_cast<u8>(1u << hud->view.planes);
                sched.emit_palette(hud->palette, 0, count);
            }
        } else if (!view.split_active || raster < 0xf8u) {
            sched.wait_line(0xf8);
            sched.move(copper::Register::COLOR00, 0x0000);
        }
        // Sprites hardware (delante de los playfields): SPRxCTL/POS/PT + paleta 16-31.
        if (m_cfg.sprites != nullptr && m_cfg.sprites->any_enabled()) {
            sched.emit_palette(m_cfg.palette, 16, 16); // paleta de sprites (COLOR16-31)
            m_cfg.sprites->emit_into(sched);
        }
        sched.end();
        m_copper_words = sched.words_used();
        m_ok = sched.ok();
        return m_ok;
    }

    Config m_cfg {};
    MemoryBlock m_copper_blocks[2] {};
    u16 m_copper_words = 0;
    u8 m_active = 0;
    bool m_initialized = false;
    bool m_copper_initialized = false;
    bool m_ok = false;
};

/// Compositor dual playfield (DPF 3+3) para dos `XLimitedPlayfield` (corkscrew).
///
/// PF1 usa los planos de hardware 1,3,5 y PF2 los 2,4,6 (cada playfield es un
/// bitmap interleaved independiente de profundidad `planes_per_field`). Ambos
/// comparten `BPLCON1` (dos nibbles de fine scroll), `BPLCON2` (prioridad) y el
/// split vertical (misma `display_height` y, si scrollean juntos en Y, misma
/// `split_line`). Cada playfield conserva su `planeaddx` (parallax en X posible).
class XlimitedDualComposer {
public:
    struct Config {
        const u16* palette = nullptr;      // 16 colores: PF1 0..7, PF2 8..15
        u32 copper_bytes = 1536;
        u8 planes_per_field = 3;           // 3+3 → 6 planos de hardware
        bool foreground_is_pf2 = false;    // BPLCON2 PF2PRI
        u16 diwstrt = xlimited_detail::kDiwStrt;
        u16 diwstop = xlimited_detail::kDiwStop;
        u16 ddfstrt = xlimited_detail::kDdfStrt;
        u16 ddfstop = xlimited_detail::kDdfStop;
    };

    bool init(MemorySystem& memory, const Config& cfg) {
        m_cfg = cfg;
        m_copper_blocks[0] = memory.chip.allocate(cfg.copper_bytes, 16);
        m_copper_blocks[1] = memory.chip.allocate(cfg.copper_bytes, 16);
        if (!m_copper_blocks[0].valid() || !m_copper_blocks[1].valid() || !cfg.palette) return false;
        m_initialized = true;
        return true;
    }

    bool compose(const PlayfieldHardwareView& pf1, const PlayfieldHardwareView& pf2) {
        if (!m_initialized) return false;
        if (!valid(pf1, pf2)) return false;
        if (!m_copper_initialized) {
            if (!emit_full(0, pf1, pf2) || !emit_full(1, pf1, pf2)) return false;
            m_copper_initialized = true;
            m_active = 0;
            return true;
        }
        const u8 inactive = static_cast<u8>(m_active ^ 1u);
        if (!emit_full(inactive, pf1, pf2)) return false;
        m_active = inactive;
        return true;
    }

    template <typename Backend>
    void install(Backend& backend) const {
        if (m_initialized && m_copper_initialized) {
            backend.install_copper_list(static_cast<const u16*>(m_copper_blocks[m_active].data));
        }
    }

    constexpr bool ok() const { return m_ok; }
    constexpr u16 copper_words() const { return m_copper_words; }

private:
    static constexpr u8 hardware_plane(u8 pf1_plane, bool is_pf1) {
        // PF1 → planos 1,3,5 (índices 0,2,4); PF2 → 2,4,6 (índices 1,3,5).
        return static_cast<u8>(pf1_plane * 2u + (is_pf1 ? 0u : 1u));
    }

    static u32 field_plane_address(const PlayfieldHardwareView& v, u8 plane, u32 y_offset) {
        return reinterpret_cast<u32>(v.real_base) + v.planeaddx + y_offset +
               static_cast<u32>(plane) * v.bitmap_bytes_per_row;
    }

    bool valid(const PlayfieldHardwareView& a, const PlayfieldHardwareView& b) const {
        g_dbg_dual_valid = 0;
        if (!a.bitplanes || !b.bitplanes) { return false; }
        if (a.planes != m_cfg.planes_per_field || b.planes != m_cfg.planes_per_field) { return false; }
        if (a.planes + b.planes > 6) { return false; }
        // El FG (b) puede ser ESTÁTICO (CanvasPlayfield, sin corkscrew): su
        // display_height es el viewport y no tiene split (no envuelve). El BG (a)
        // es el corkscrew (bucle + split). Solo si b también envuelve deben
        // coincidir display_height y split_line.
        if (b.display_height != a.display_height && b.display_height != a.viewport_h) { return false; }
        if (b.split_active) {
            if (a.split_active != b.split_active) { return false; }
            if (a.split_line != b.split_line) { return false; }
        }
        return true;
    }

    bool emit_full(u8 block, const PlayfieldHardwareView& pf1, const PlayfieldHardwareView& pf2) {
        copper::Scheduler sched { m_copper_blocks[block] };
        const u8 total = static_cast<u8>(m_cfg.planes_per_field * 2u);
        const u16 bplcon0 = static_cast<u16>(0x0200u | (static_cast<u16>(total) << 12u) | 0x0400u);
        // BPLCON1: nibble bajo = fine de PF1, alto = fine de PF2.
        const u16 bplcon1 = static_cast<u16>(
            ((pf2.bplcon1 & 0x0f) << 4) | (pf1.bplcon1 & 0x0f));
        sched.move(copper::Register::DMACON,
            static_cast<u16>(copper::DmaSetClear | copper::DmaMaster |
                             copper::DmaCopper | copper::DmaBitplane));
        sched.move(copper::Register::BPLCON0, bplcon0);
        sched.move(copper::Register::BPLCON1, bplcon1);
        sched.move(copper::Register::BPLCON2, m_cfg.foreground_is_pf2 ? 0x0040u : 0x0000u);
        sched.move(copper::Register::BPL1MOD, pf1.bpl1mod); // planos 1,3,5
        sched.move(copper::Register::BPL2MOD, pf2.bpl1mod); // planos 2,4,6
        sched.move(copper::Register::DIWSTRT, m_cfg.diwstrt);
        sched.move(copper::Register::DIWSTOP, m_cfg.diwstop);
        sched.move(copper::Register::DDFSTRT, m_cfg.ddfstrt);
        sched.move(copper::Register::DDFSTOP, m_cfg.ddfstop);
        sched.emit_palette(m_cfg.palette, 0, 16); // DPF: 16 colores (PF1 0..7, PF2 8..15)
        for (u8 i = 0; i < m_cfg.planes_per_field; ++i) {
            const u8 hw1 = hardware_plane(i, true);
            const u8 hw2 = hardware_plane(i, false);
            sched.move_bitplane_pointer(hw1, reinterpret_cast<const void*>(
                field_plane_address(pf1, i, pf1.planeaddy)));
            sched.move_bitplane_pointer(hw2, reinterpret_cast<const void*>(
                field_plane_address(pf2, i, pf2.planeaddy)));
        }
        u16 raster = 0;
        if (pf1.split_active) {
            raster = static_cast<u16>((m_cfg.diwstrt >> 8u) + pf1.split_line);
            const u8 wait = raster > 0xffu ? 0xffu : static_cast<u8>(raster);
            sched.wait_line(wait);
            for (u8 i = 0; i < m_cfg.planes_per_field; ++i) {
                sched.move_bitplane_pointer(hardware_plane(i, true),
                    reinterpret_cast<const void*>(field_plane_address(pf1, i, pf1.split_planeaddy)));
                // FG estático (lienzo): no envuelve, sus punteros no cambian en el split.
                if (pf2.split_active) {
                    sched.move_bitplane_pointer(hardware_plane(i, false),
                        reinterpret_cast<const void*>(field_plane_address(pf2, i, pf2.split_planeaddy)));
                }
            }
        }
        if (!pf1.split_active || raster < 0xf8u) {
            sched.wait_line(0xf8);
            sched.move(copper::Register::COLOR00, 0x0000);
        }
        sched.end();
        m_copper_words = sched.words_used();
        m_ok = sched.ok();
        return m_ok;
    }

    Config m_cfg {};
    MemoryBlock m_copper_blocks[2] {};
    u16 m_copper_words = 0;
    u8 m_active = 0;
    bool m_initialized = false;
    bool m_copper_initialized = false;
    bool m_ok = false;
};

} // namespace eng::field

/// Depuración: resultado del `valid()` del compositor dual (0 = OK).
volatile eng::u32 eng::field::g_dbg_dual_valid = 0;



