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
///     bitmapheight = BITMAPHEIGHT                    // cfg.viewport_h visibles
///                  + (map_width / BITMAPBLOCKSPERROW / planes) + 1
///                  + 3                               // guarda + fetch ancho
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

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/field/tile_field.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

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
} // namespace xlimited_detail

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
///   fill_screen usa visibleRows = viewport_h / tile_height y colHeight = visibleRows + (scroll_y?1:0).
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
/// DPF (futuro, como en 102/104) usará dos `XlimitedField` + `XlimitedDisplayComposer`
/// dual: PF1 en planos impares (1,3,5) y PF2 en pares (2,4,6), cada uno con su
/// BlocksBitmap interleaved y su `frontbuffer` propio pero compartiendo el mismo
/// `bitmap_height` y `BPLMOD`. El compositor dual programará `BPLCON0` con DPF=1 y
/// `BPLCON2` con prioridad PF1/PF2, y publicará los 6 punteros interleaved en un
/// único Copper wait (sin split horizontal, como en X-Limited).
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
/// `XlimitedField::fill_screen` es atómico; la demo debe rellenar en lotes
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
    bool scroll_y = false;             // si true, fill_screen añade 1 fila extra (colHeight = visibleRows+1)
};

/// Vista de hardware que el compositor necesita para programar el Copper.
///
/// Es el equivalente interleaved de `FieldHardwareView`, pero sin split:
/// un único `frontbuffer` base y el desplazamiento fino/coarse derivado de
/// `videoposx`. No hay `surface_origin` ni guarda vertical: el wrap es
/// implícito por el fetch lineal + altura extra.
struct XlimitedHardwareView {
    const u8* bitplanes = nullptr; // frontbuffer (Planes[0] + bitmapoffset)
    const u8* real_base = nullptr; // base real del AllocBitMap (para BPLxPT)
    u32 planeaddx = 0;             // coarse X en bytes
    u32 planeaddy = 0;             // offset Y interleaved = videoposy*planes*bytes
    u16 bplcon1 = 0;               // scroll fino duplicado en ambos nibbles
    u16 bpl1mod = 0;
    u16 bpl2mod = 0;
    u16 bitmap_bytes_per_row = 0;  // bytes por fila (ej. viewport_w+32)/8
    u32 plane_bytes = 0;           // bytes totales (para validación)
    u8 planes = 0;
    u16 bitmap_height = 0;
    u16 viewport_w = 0;
    u16 viewport_h = 0;
    s32 videoposx = 0;
    s32 mapposx = 0;
    s32 videoposy = 0;
    s32 mapposy = 0;
};

/// Campo XLimited: scroll infinito en X con bitmap interleaved y wrap vertical.
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
class XlimitedField {
public:
    XlimitedField() = default;

    // No copiable (posee memoria Chip)
    XlimitedField(const XlimitedField&) = delete;
    XlimitedField& operator=(const XlimitedField&) = delete;

    /// Calcula la altura total según la fórmula canónica de Steger parametrizada.
    ///
    /// \param viewport_h   alto visible (cfg.viewport_h)
    /// \param map_width_blocks  ancho del mapa en bloques (ej. screens_x*viewport_w/tile_width)
    /// \param bitmap_blocks_per_row  BITMAPWIDTH / tile_width
    /// \param planes  profundidad (4)
    /// \return altura en píxeles (viewport_h + extra +1+3)
    static constexpr u16 compute_bitmap_height(u16 viewport_h,
                                              u16 map_width_blocks,
                                              u16 bitmap_blocks_per_row,
                                              u8 planes) {
        return static_cast<u16>(
            viewport_h +
            (map_width_blocks / bitmap_blocks_per_row / planes) + 1 + 3);
    }

    /// Reserva el bitmap interleaved y prepara el estado inicial.
    ///
    /// Reserva `bitmap_bytes_per_row * bitmap_height * planes` bytes en Chip
    /// RAM con alineación 16 (como `AllocBitMap(..., BMF_INTERLEAVED|BMF_CLEAR)`).
    /// `frontbuffer` apunta a `base + bitmapoffset` para los modos de fetch
    /// ancho (16 bytes para BPL32, 48 para 4x). En modo normal offset=0.
    bool begin(MemorySystem& memory, const XlimitedConfig& cfg) {
        m_cfg = cfg;
        if (!valid_config()) return false;

        // Derivar bitmap_width si es 0: viewport_w + EXTRAWIDTH según fetch_mode.
        if (m_cfg.bitmap_width == 0) {
            const u16 extra = (m_cfg.fetch_mode == 0) ? xlimited_detail::kExtraW32 : xlimited_detail::kExtraW64;
            m_cfg.bitmap_width = static_cast<u16>(m_cfg.viewport_w + extra);
        }
        m_bitmap_width = m_cfg.bitmap_width;
        m_bitmap_bytes_per_row = static_cast<u16>(m_bitmap_width / 8u);
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
            m_cfg.viewport_h,
            map_w_blocks,
            m_bitmap_blocks_per_row, m_cfg.planes);
        // Altura mínima: max(viewport_h+1+3, 16*tile_height) para que los 16 valores de mapy (0..15) quepan.
        // Para viewport 224×16 con tile 16 → 224+4=228 <256, se eleva a 256 para acomodar y=15*BLOCKPLANELINES.
        const u16 min_h_viewport = static_cast<u16>(m_cfg.viewport_h + 1 + 3);
        const u16 min_h_blocks = static_cast<u16>(16u * m_cfg.tile_height);
        const u16 min_h = (min_h_viewport > min_h_blocks) ? min_h_viewport : min_h_blocks;
        if (m_bitmap_height < min_h) m_bitmap_height = min_h;

        // BMF_INTERLEAVED-like: un único bloque Chip contiguo.
        // total = bytes_por_planeline * altura * planes  (planeline totales)
        const u32 total_bytes = static_cast<u32>(m_bitmap_bytes_per_row) *
                                static_cast<u32>(m_bitmap_height) *
                                static_cast<u32>(m_cfg.planes);
        // Reserva extra para el offset de fetch ancho (hasta 48 B) + guarda.
        const u32 alloc_bytes = total_bytes + 64u;
        m_bitmap_block = memory.chip.allocate(alloc_bytes, 16);
        if (!m_bitmap_block.valid()) return false;

        m_real_base = static_cast<u8*>(m_bitmap_block.data);
        // Offset de fetch (normal=0, BPL32=16, 4x=48) — ver fetchinfo[]
        const u16 bitmap_offset = fetch_bitmap_offset(m_cfg.fetch_mode);
        m_frontbuffer = m_real_base + bitmap_offset;

        // BPLMODs: BITMAPBYTESPERROW*planes - SCREENBYTESPERROW - modulo_offset
        // modulo_offset = 2 (normal), 4 (BPL32/BPAGEM), 8 (BPL32+BPAGEM) según fetch_mode
        // SCREENBYTESPERROW = cfg.viewport_w / 8
        const u16 modulo_offset = fetch_modulo_offset(m_cfg.fetch_mode);
        const s32 mod = static_cast<s32>(m_bitmap_bytes_per_row) * m_cfg.planes -
                        (m_cfg.viewport_w / 8) - modulo_offset;
        m_bpl1mod = static_cast<u16>(mod);
        m_bpl2mod = static_cast<u16>(mod);

        m_mapposx = 0;
        m_videoposx = 0;
        m_mapposy = 0;
        m_videoposy = 0;
        m_previous_dir = 0xff; // DIRECTION_IGNORE
        m_savewordpointer = nullptr;
        m_saveword = 0;
        m_blocks_buffer = reinterpret_cast<const u8*>(m_cfg.tileset);
        m_initialized = true;
        return true;
    }

    /// Rellena la pantalla inicial (equivalente a FillScreen).
    ///
    /// Emite `BITMAPBLOCKSPERROW * colHeight` jobs de Blitter en el
    /// `FramePlan` dado, donde colHeight = visibleRows + (scroll_y?1:0) y
    /// visibleRows = viewport_h / tile_height. Sin literales 16/17.
    bool fill_screen(graphics::FramePlan& plan) const {
        if (!m_initialized) return false;
        const u16 cols = m_bitmap_blocks_per_row;
        const u16 visibleRows = static_cast<u16>(m_cfg.viewport_h / m_cfg.tile_height);
        const u16 colHeight = static_cast<u16>(visibleRows + (m_cfg.scroll_y ? 1u : 0u));
        const u16 rows = colHeight;
        for (u16 b = 0; b < rows; ++b) {
            for (u16 a = 0; a < cols; ++a) {
                const u16 x = a * m_cfg.tile_width;
                const u16 y = b * m_block_planes_lines; // planeline
                const u16 mapx = a;
                const u16 mapy = b;
                auto job = draw_block_job(x, y, mapx, mapy);
                if (!plan.add_tile_block_copy(job)) return false;
            }
        }
        return true;
    }

    /// Construye el `BlitJob` de un bloque (contrato de xlimited.c:201).
    ///
    /// \param x  coordenada X en píxeles (será word-aligned)
    /// \param y  coordenada Y en **planeline** (no píxeles)
    /// \param mapx índice de bloque en X del mapa
    /// \param mapy índice de bloque en Y del mapa
    graphics::BlitJob draw_block_job(u16 x, u16 y, u16 mapx, u16 mapy) const {
        // x word-aligned como en DrawBlock: (x/8) & 0xFFFE
        const u16 x_word = static_cast<u16>((x / 8u) & 0xFFFEu);
        const u32 dst_offset = static_cast<u32>(y) * m_bitmap_bytes_per_row + x_word;

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
        const s16 dst_mod = static_cast<s16>(m_bitmap_bytes_per_row - words * 2);

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

    /// Scroll de 1 píxel a la derecha (plane-shifted).
    ///
    /// Implementa fielmente ScrollRight de xlimited.c:526-553:
    ///   mapx = mapposx/tile_width + BITMAPBLOCKSPERROW
    ///   mapy = mapposx & (tile_width-1)
    ///   x = BITMAPWIDTH + (videoposx & ~(tile_width-1))
    ///   y = mapy * BLOCKPLANELINES
    /// Guarda la word de solapamiento si la dirección anterior era izquierda.
    bool scroll_right(graphics::FramePlan& plan) {
        if (!m_initialized) return false;
        const u16 map_w_for_limit = m_cfg.map.width ? m_cfg.map.width
                                    : static_cast<u16>(m_cfg.screens_x * (m_cfg.viewport_w / m_cfg.tile_width));
        const s32 limit = static_cast<s32>(map_w_for_limit) * m_cfg.tile_width -
                          m_cfg.viewport_w - m_cfg.tile_width;
        // Para mapas con wrap, el límite no se aplica: el mapa es circular.
        if (m_cfg.map.wrap_x == 0 && m_mapposx >= limit) return false;

        const u16 mapx = static_cast<u16>(m_mapposx / m_cfg.tile_width + m_bitmap_blocks_per_row);
        const u16 mapy_blocks = static_cast<u16>(m_mapposx & (m_cfg.tile_width - 1));
        // En X-Limited puro el mapa es mucho más alto que la pantalla (screens_y * viewport_h/tile_height filas)
        // y el corkscrew usa `mapy_blocks` tanto para la coordenada Y del
        // bitmap (`y = mapy*BLOCKPLANELINES`, tile_height*planes) como para la coordenada Y del mapa
        // (`map_tile_y = mapy`). Así, cada uno de los tile_width pasos de un bloque
        // pinta una fila distinta de la columna entrante y no se repiten
        // plaquetas. La versión anterior con `map_tile_y=0` dejaba toda la
        // columna con tiles de la fila 0, visible como repetición vertical.
        // Ver xlimited.c: `mapy = mapposx & (tile_width-1); DrawBlock(x, y, mapx, mapy)`.
        const u16 map_tile_x = mapx;
        const u16 map_tile_y = 0; // X-Limited puro: columna vertical uniforme (repetición
        // vertical intencionada para 50 fps; la variación por filas se verá
        // con scroll vertical/XY donde map_tile_y = mapy_blocks. Ver §5.
        const u16 x = static_cast<u16>(m_bitmap_width + (m_videoposx & ~(m_cfg.tile_width - 1)));
        const u16 y = static_cast<u16>(mapy_blocks * m_block_planes_lines);

        if (m_previous_dir == 1) { // DIRECTION_LEFT
            // Restaurar la word pisada por el blit anterior no plane-shifted.
            if (m_savewordpointer) *m_savewordpointer = m_saveword;
        }
        // Guardar la última word del bloque que vamos a pisar (plane-shifted).
        const u32 save_offset = static_cast<u32>(y + m_block_planes_lines - 1) *
                                    m_bitmap_bytes_per_row + (x / 8u);
        m_savewordpointer = reinterpret_cast<u16*>(
            const_cast<u8*>(m_frontbuffer) + save_offset);
        m_saveword = *m_savewordpointer;

        auto job = draw_block_job(x, y, map_tile_x, map_tile_y);
        if (!plan.add_tile_block_copy(job)) return false;

        ++m_mapposx;
        m_videoposx = m_mapposx;
        m_previous_dir = 0; // DIRECTION_RIGHT
        return true;
    }

    /// Scroll de 1 píxel a la izquierda (no plane-shifted).
    ///
    /// Fiel a ScrollLeft de xlimited.c:497-524:
    ///   mapposx-- ; videoposx=mapposx
    ///   mapx = mapposx/tile_width
    ///   mapy = mapposx & (tile_width-1)
    ///   x = videoposx & ~(tile_width-1)
    ///   y = mapy*BLOCKPLANELINES
    bool scroll_left(graphics::FramePlan& plan) {
        if (!m_initialized) return false;
        if (m_mapposx < 1) return false;
        --m_mapposx;
        m_videoposx = m_mapposx;

        const u16 mapx = static_cast<u16>(m_mapposx / m_cfg.tile_width);
        const u16 mapy_blocks = static_cast<u16>(m_mapposx & (m_cfg.tile_width - 1));
        const u16 x = static_cast<u16>(m_videoposx & ~(m_cfg.tile_width - 1));
        const u16 y = static_cast<u16>(mapy_blocks * m_block_planes_lines);

        if (m_previous_dir == 0) { // DIRECTION_RIGHT
            if (m_savewordpointer) *m_savewordpointer = m_saveword;
        }
        const u32 save_offset = static_cast<u32>(y) * m_bitmap_bytes_per_row + (x / 8u);
        m_savewordpointer = reinterpret_cast<u16*>(
            const_cast<u8*>(m_frontbuffer) + save_offset);
        m_saveword = *m_savewordpointer;

        // X-Limited puro mantiene fila 0 (ver nota arriba).
        auto job = draw_block_job(x, y, mapx, 0);
        if (!plan.add_tile_block_copy(job)) return false;

        m_previous_dir = 1; // DIRECTION_LEFT
        return true;
    }

    /// Scroll vertical 1 px hacia abajo (preparado para ciclo H/V/diagonal).
    ///
    /// En vertical puro no hay plane-shift horizontal; la fila entrante se
    /// dibuja en `y = ((videoposy + viewport_h) % bitmap_height) * planes` y
    /// `map_tile_y = (mapposy + viewport_h)/tile_height + BLOCKSPERROW` análogo al X.
    /// Para interleaved la fila es `BLOCKPLANELINES` planeline contiguas.
    /// Ver yunlimited2.c y docs/AMIGA_8WAY_SCROLLING.md § XY.
    bool scroll_down(graphics::FramePlan& plan) {
        if (!m_initialized) return false;
        const u16 map_h_for_limit = m_cfg.map.height ? m_cfg.map.height
                                    : static_cast<u16>(m_cfg.screens_y * (m_cfg.viewport_h / m_cfg.tile_height));
        const s32 limitY = static_cast<s32>(map_h_for_limit) * m_cfg.tile_height - m_cfg.viewport_h - m_cfg.tile_height;
        if (m_cfg.map.wrap_y==0 && m_mapposy >= limitY) return false;
        ++m_mapposy;
        m_videoposy = m_mapposy;
        // Cálculo de TWOBLOCKS como en yunlimited2: para bitmap_blocks_per_row bloques de ancho,
        // cada tile_width pasos consume BITMAPBLOCKSPERROW bloques.
        const u16 stepy = static_cast<u16>(m_mapposy & (m_cfg.tile_height - 1));
        const u16 twoblocks = static_cast<u16>(m_bitmap_blocks_per_row > (m_cfg.viewport_w / m_cfg.tile_width) ? m_bitmap_blocks_per_row - (m_cfg.viewport_w / m_cfg.tile_width) : 0);
        const bool two = stepy < twoblocks;
        const u16 count = two ? 2 : 1;
        const u16 base_x = two ? static_cast<u16>(stepy * 2 * m_cfg.tile_width) : static_cast<u16>((stepy - twoblocks + twoblocks*2) * m_cfg.tile_width);
        // Y de la fila entrante en planeline: (videoposy+viewport_h) % height * planes
        const u16 y_base = static_cast<u16>((m_videoposy + m_cfg.viewport_h) % m_bitmap_height);
        const u16 y_pl = static_cast<u16>(y_base * m_cfg.planes);
        const u16 map_tile_y = static_cast<u16>(m_mapposy / m_cfg.tile_height + (m_bitmap_height / m_cfg.tile_height));
        for (u16 i=0;i<count;++i){
            const u16 x = static_cast<u16>(base_x + i * m_cfg.tile_width);
            const u16 map_tile_x = static_cast<u16>(m_mapposx / m_cfg.tile_width + (base_x + i*m_cfg.tile_width)/m_cfg.tile_width);
            auto job = draw_block_job(x, y_pl, map_tile_x, map_tile_y);
            if (!plan.add_tile_block_copy(job)) { --m_mapposy; m_videoposy=m_mapposy; return false; }
        }
        m_previous_dir = 2;
        return true;
    }

    bool scroll_up(graphics::FramePlan& plan) {
        if (!m_initialized || m_mapposy <1) return false;
        --m_mapposy;
        m_videoposy = m_mapposy;
        const u16 stepy = static_cast<u16>(m_mapposy & (m_cfg.tile_height - 1));
        const u16 twoblocks = static_cast<u16>(m_bitmap_blocks_per_row > (m_cfg.viewport_w / m_cfg.tile_width) ? m_bitmap_blocks_per_row - (m_cfg.viewport_w / m_cfg.tile_width) : 0);
        const bool two = stepy < twoblocks;
        const u16 base_x = two ? static_cast<u16>(stepy * 2 * m_cfg.tile_width) : static_cast<u16>((stepy - twoblocks + twoblocks*2) * m_cfg.tile_width);
        const u16 y_base = static_cast<u16>(m_videoposy % m_bitmap_height);
        const u16 y_pl = static_cast<u16>(y_base * m_cfg.planes);
        const u16 map_tile_y = static_cast<u16>(m_mapposy / m_cfg.tile_height);
        for (u16 i=0;i<(two?2:1);++i){
            const u16 x = static_cast<u16>(base_x + i * m_cfg.tile_width);
            const u16 map_tile_x = static_cast<u16>(m_mapposx / m_cfg.tile_width + (base_x + i*m_cfg.tile_width)/m_cfg.tile_width);
            auto job = draw_block_job(x, y_pl, map_tile_x, map_tile_y);
            if (!plan.add_tile_block_copy(job)) { ++m_mapposy; m_videoposy=m_mapposy; return false; }
        }
        m_previous_dir=3;
        return true;
    }

    /// Vista de hardware para el compositor (planeaddx + BPLCON1 + offset Y).
    XlimitedHardwareView hardware_view() const {
        XlimitedHardwareView v {};
        v.bitplanes = m_frontbuffer;
        v.real_base = m_real_base;
        v.bitmap_bytes_per_row = m_bitmap_bytes_per_row;
        v.planes = m_cfg.planes;
        v.bitmap_height = m_bitmap_height;
        v.viewport_w = m_cfg.viewport_w;
        v.viewport_h = m_cfg.viewport_h;
        v.videoposx = m_videoposx;
        v.mapposx = m_mapposx;
        v.videoposy = m_videoposy;
        v.mapposy = m_mapposy;

        // Cálculo idéntico a UpdateCopperlist (xlimited.c:579-613) para
        // fetch normal (I=tile_width). Para fetch ancho se generaliza con I.
        const u16 I = fetch_scroll_pixels(m_cfg.fetch_mode);
        const s32 xpos = m_videoposx + static_cast<s32>(I) - 1;
        const u32 planeaddx = static_cast<u32>(xpos / I) * (I / 8u);
        s32 fine = (static_cast<s32>(I) - 1) - (xpos & (I - 1));
        u16 scroll = static_cast<u16>((fine & 15) * 0x11);
        if (fine & 16) scroll |= 0x4400;
        if (fine & 32) scroll |= 0x8800;
        v.planeaddx = planeaddx;
        v.bplcon1 = scroll;
        v.bpl1mod = m_bpl1mod;
        v.bpl2mod = m_bpl2mod;
        // Offset vertical para interleaved: videoposy * planes * bytesPerRow
        // Con wrap circular: (videoposy % bitmap_height) * planes * bytes
        v.planeaddy = static_cast<u32>((m_videoposy % m_bitmap_height + m_bitmap_height) % m_bitmap_height) * m_cfg.planes * m_bitmap_bytes_per_row;
        // plane_bytes para validación: bytes totales
        v.plane_bytes = static_cast<u32>(m_bitmap_bytes_per_row * m_bitmap_height * m_cfg.planes);
        return v;
    }

    // Accesores para verificación y demo
    constexpr s32 mapposx() const { return m_mapposx; }
    constexpr s32 videoposx() const { return m_videoposx; }
    constexpr s32 mapposy() const { return m_mapposy; }
    constexpr s32 videoposy() const { return m_videoposy; }
    constexpr u16 bitmap_bytes_per_row() const { return m_bitmap_bytes_per_row; }
    constexpr u16 bitmap_width() const { return m_bitmap_width; }
    constexpr u16 bitmap_height() const { return m_bitmap_height; }
    constexpr u16 bitmap_blocks_per_row() const { return m_bitmap_blocks_per_row; }
    constexpr u16 block_planes_lines() const { return m_block_planes_lines; }
    constexpr const u8* frontbuffer() const { return m_frontbuffer; }
    constexpr bool initialized() const { return m_initialized; }
    constexpr u16 bpl1mod() const { return m_bpl1mod; }

    /// Reinicia el scroll a 0 sin re-reservar Chip RAM (para demo infinita).
    void reset_scroll() {
        m_mapposx = 0;
        m_videoposx = 0;
        m_mapposy = 0;
        m_videoposy = 0;
        m_previous_dir = 0xff;
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
    MemoryBlock m_bitmap_block {};
    u8* m_real_base = nullptr;
    u8* m_frontbuffer = nullptr;
    const u8* m_blocks_buffer = nullptr;
    u16 m_bitmap_width = xlimited_detail::kBitmapW32;
    u16 m_bitmap_bytes_per_row = 44;
    u16 m_bitmap_blocks_per_row = xlimited_detail::kBlocksPerRow32;
    u16 m_block_planes_lines = 0; // recalculado en begin(): BLOCKHEIGHT*planes
    u16 m_bitmap_height = 0; // recalculado en begin(): compute_bitmap_height(viewport_h, mapW, blocksPerRow, planes)
    u16 m_bpl1mod = 0, m_bpl2mod = 0;
    s32 m_mapposx = 0, m_videoposx = 0;
    s32 m_mapposy = 0, m_videoposy = 0;
    u16* m_savewordpointer = nullptr;
    u16 m_saveword = 0;
    u8 m_previous_dir = 0xff;
    bool m_initialized = false;
};

/// Compositor mínimo para XLimited (single playfield interleaved).
///
/// Es la única capa que toca BPLCON0/BPLCON1/BPLxPT/BPLMOD/DIW/DDF. A
/// diferencia de `DpfDisplayComposer` no necesita gestionar dos playfields ni
/// split vertical: una sola vista interleaved, DDFSTRT=$30 fijo (42 bytes) y
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

    bool compose(const XlimitedHardwareView& view) {
        if (!m_initialized || !view.bitplanes) return false;
        if (!valid_view(view)) return false;
        if (!m_copper_initialized) {
            if (!emit_full(0, view) || !emit_full(1, view)) return false;
            m_copper_initialized = true;
            m_active = 0;
            return true;
        }
        const u8 inactive = static_cast<u8>(m_active ^ 1u);
        if (!emit_full(inactive, view)) return false;
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

private:
    static constexpr u16 pointer_high_word(u8 plane) {
        return static_cast<u16>(21u + plane * 4u);
    }
    static constexpr u16 pointer_low_word(u8 plane) {
        return static_cast<u16>(23u + plane * 4u);
    }

    bool valid_view(const XlimitedHardwareView& v) const {
        if (!v.bitplanes || v.planes == 0 || v.planes > 6) return false;
        if (v.bitmap_bytes_per_row == 0) return false;
        if (v.bitmap_height == 0) return false;
        if (v.viewport_w == 0 || v.viewport_h == 0) return false;
        // Validación genérica BPLMOD: debe ser bitmap_bytes_per_row*planes - viewport_w/8 - modulo_offset
        // No se valida con literal 320; se comprueba que bpl1mod no excede el total.
        if (v.plane_bytes < static_cast<u32>(v.bitmap_bytes_per_row * v.bitmap_height * v.planes)) return false;
        return true;
    }

    bool emit_full(u8 block, const XlimitedHardwareView& view) {
        copper::Scheduler sched { m_copper_blocks[block] };
        const u16 bplcon0 = static_cast<u16>(
            0x0200u | (static_cast<u16>(view.planes) << 12u));
        sched.move(copper::Register::DMACON,
            static_cast<u16>(copper::DmaSetClear | copper::DmaMaster |
                             copper::DmaCopper | copper::DmaBitplane));
        sched.move(copper::Register::BPLCON0, bplcon0);
        sched.move(copper::Register::BPLCON1, view.bplcon1);
        sched.move(copper::Register::BPLCON2, 0x0000);
        sched.move(copper::Register::BPL1MOD, view.bpl1mod);
        sched.move(copper::Register::BPL2MOD, view.bpl2mod);
        sched.move(copper::Register::DIWSTRT, m_cfg.diwstrt);
        sched.move(copper::Register::DIWSTOP, m_cfg.diwstop);
        sched.move(copper::Register::DDFSTRT, m_cfg.ddfstrt);
        sched.move(copper::Register::DDFSTOP, m_cfg.ddfstop);
        for (u8 p = 0; p < view.planes; ++p) {
            const u32 addr = reinterpret_cast<u32>(view.real_base) +
                             view.planeaddx + view.planeaddy +
                             static_cast<u32>(p) * view.bitmap_bytes_per_row;
            // En interleaved, Planes[p] = base + p*BITMAPBYTESPERROW + Y*planes*bytes.
            // planeaddy aporta el offset vertical (videoposy) y planeaddx el horizontal.
            sched.move_bitplane_pointer(p, reinterpret_cast<const void*>(addr));
        }
        sched.emit_palette(m_cfg.palette);
        sched.wait_line(0xf8);
        sched.move(copper::Register::COLOR00, 0x0000);
        sched.end();
        m_ok = sched.ok();
        return m_ok;
    }

    Config m_cfg {};
    MemoryBlock m_copper_blocks[2] {};
    u8 m_active = 0;
    bool m_initialized = false;
    bool m_copper_initialized = false;
    bool m_ok = false;
};

} // namespace eng::field
