#pragma once

/// \file xlimited_base.hpp
/// Implementación fiel de la FAMILIA **x-limited** de Georg Steger
/// (ScrollingTricks) sin arrastrar la máquina circular de `TileFieldController`.
///
/// NOMENCLATURA: "x-limited" es la familia. La variante SÓLO horizontal es
/// `Scroller_XLimited` (sin split); la variante 8-way con anillo vertical,
/// staging y split es `Scroller_XYLimited` (el "corkscrew"). En este engine la
/// variante se elige con `AxisPolicy::Ring` en `y_mode` (corkscrew); cuando está
/// activa, el algoritmo correcto es **XYLimited**, no XLimited. La demo 201
/// (`y_mode = Ring`) es XYLimited.
///
/// Este header es *didáctico*: cada sección explica el invariante del
/// hardware que mantiene y por qué una alternativa aparentemente más simple
/// costaría más CPU/Blitter/Chip RAM. Todos los comentarios tutoriales están
/// en español con ortografía correcta.
///
/// ```text
///   Ring (corkscrew): el bitmap es un ANILLO vertical de display_height ≈ SCREENHEIGHT + EXTRAHEIGHT
///   ┌───────────────────────────────┐  ◄─ base (BPLxPT), línea 0
///   │ ventana visible               │
///   ├───────────────────────────────┤  línea split_line: el display ENVUELVE aquí
///   │ banda de staging (2 bloques)  │     (display_offset avanza por frame)
///   │  (se pre-pinta la fila nueva) │
///   └───────────────────────────────┘  ◄─ vuelve a la base (wrap a display_height)
///   Al cruzar el split se re-pinta la banda entrante en staging ANTES de que el display la alcance.
///   Variante SÓLO horizontal (Scroller_XLimited) → sin split ni staging.
/// ```
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
///   `display_height` (ver §13 de docs/engine/architecture/AMIGA_8WAY_SCROLLING.md).
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
///   Referencias: docs/engine/architecture/AMIGA_8WAY_SCROLLING.md (contraste con el
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
///   - **Runtime:** `demos/amiga/107_xlimited_corkscrew/analyze-sequence.sh --warp`
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
///      `bash demos/amiga/107_xlimited_corkscrew/analyze-sequence.sh --warp` (runtime)
///      y exigir `OK telemetría`, `OK columna`, `DuplicatePairs==0` y
///      `green>0` en `analyze-demo.sh`.
///
/// \see eng::field::TileLayerMap
/// \see eng::field::XlimitedDisplayComposer

#include <eng/core/fast_div.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/field/amiga_display_mapper.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/scroll_engine.hpp>
#include <eng/field/scroll_profile.hpp>
#include <eng/field/soft_dpf.hpp>
#include <eng/field/tile_map.hpp>
#include <eng/graphics/copper/double_buffer.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/playfield_scroll.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

/// Depuración: resultado del `valid()` del compositor dual (0 = OK).
extern volatile eng::u32 g_dbg_dual_valid;

// Los helpers puros del soft DPF (offsets de parallax/fijo, `bg_shift_for`,
// `bg_window_for`, `bg_split_rects`) y `SoftDpfComposition` viven en
// `soft_dpf.hpp` (incluido arriba).


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
constexpr u16 kDdfStrt = eng::graphics::fine_scroll_ddfstrt;
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

/// Política de DIRECCIÓN del scroll (vocabulario objetivo, `AxisPolicy` +
/// `DirectionPolicy`). `Bidirectional`: el scroll puede invertirse, así que en los
/// cruces hay que restaurar la costura saveword. `OneWay`: avance en un solo
/// sentido; se omite esa restauración (menos blits).
enum class DirectionPolicy : u8 { Bidirectional = 0, OneWay = 1 };

/// Política de un EJE de scroll (independiente por eje, `x_mode`/`y_mode`). Permite
/// la familia:
///   - `Ring`: anillo con banda de staging (XLimited/XYLimited). El motor escribe
///     la banda entrante (creep).
///   - `Finite`: rango acotado `[0, mundo - viewport]`, bitmap = ancho de mundo,
///     **sin anillo ni bandas de guarda**; el puntero se mueve directamente y el
///     contenido ya está (lo pinta `fill_screen`). Para el eje corto de un shooter
///     (p. ej. 400 px de ancho con ventana de 320).
///   - `Off`: el eje no scrollea.
///
/// El Y de un juego de scroll largo vertical usa `y_mode = Ring` (corkscrew); el X
/// corto usa `Finite`. `DirectionPolicy::OneWay` aplica a ambos ejes.
enum class AxisPolicy : u8 { Ring = 0, Finite = 1, Off = 2 };

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
/// La demo 107 es el *showcase* del corkscrew y la 202 el de DPF (dual) sobre
/// XYLimited. Los parámetros siguen el mismo patrón que 104/202 para que
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
///   bash ./tools/build/build-demo.sh demos/amiga/107_xlimited_corkscrew --debug --clean
/// bash demos/amiga/107_xlimited_corkscrew/analyze-sequence.sh --warp
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
/// bash demos/amiga/107_xlimited_corkscrew/analyze-sequence.sh --warp
/// # sin --warp para evaluar suavidad a 50 fps (warp=false por defecto en run-demo)
/// bash ./tools/test-regression.sh --demo demos/amiga/107_xlimited_corkscrew --warp
/// # Barrido de parámetros (cuando DPF esté implementado):
/// bash ./tools/test-regression.sh --demo demos/amiga/107_xlimited_corkscrew --warp --keep-going
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
template <class MapT = TileLayerMap>
struct XlimitedConfigT {
    static_assert(TileMap<MapT>, "MapT debe cumplir TileMap");
    MapT map {};
    const u16* tileset = nullptr;      // banco de bloques (BlocksBitmap->Planes[0])
    u16 tileset_count = 0;
    u8 planes = 4;                     // BLOCKSDEPTH, rango 3..6 (3=8c, 4=16c, 5=32c, 6=EHB/DPF 3+3)
    u16 tile_width = 16;               // múltiplo de 16 (16 ó 32 para la demo)
    u16 tile_height = 16;              // BLOCKHEIGHT; BLOCKPLANELINES = tile_height*planes (48/64/80/96)
    u16 bitmap_width = 0;              // 0=auto: viewport_w + EXTRAWIDTH (32 ó 64 según fetch_mode); si !=0 se respeta
    u8 fetch_mode = 0;                 // 0=normal 16px, 1=BPL32, 2=BPAGEM, 3=BPL32+BPAGEM
    u16 viewport_w = xlimited_detail::kScreenW; // ancho visible (320 por defecto, 288 alternativo)
    u16 viewport_h = xlimited_detail::kScreenH; // alto visible (256 por defecto, 224 alternativo)
    u16 display_height = 0;             // 0 = auto: viewport_h + (scroll_y ? 2*tile_height : 0).
                                        // Si !=0, define el bucle vertical del corkscrew (anillo) con
                                        // INDEPENDENCIA del alto visible. Es imprescindible cuando una
                                        // franja (p. ej. HUD) reduce `viewport_h`: el walk plane-shifted
                                        // del scroll horizontal necesita el anillo completo (SCREENHEIGHT +
                                        // 2*tile_height, 18 bloques) para que `mapy` (hasta 17) no colisione.
    u8 screens_x = 16;                 // pantallas virtuales en X (map_w = screens_x * viewport_w/tile_width)
    u8 screens_y = 16;                 // pantallas virtuales en Y (map_h = screens_y * viewport_h/tile_height)
    AxisPolicy x_mode = AxisPolicy::Ring;  // Ring = XLimited (anillo X, por defecto);
                                           // Finite = X lineal acotado [0, mundo-viewport] sin guardas;
                                           // Off = sin scroll X. Ver `AxisPolicy`.
    AxisPolicy y_mode = AxisPolicy::Off;   // Off = X-only (sin corkscrew); Ring = corkscrew/XY
                                           // (display_height = viewport_h + 2*tile_height,
                                           // banda de staging, fill de display_blocks_per_col, split).
    // Parallax por plano (RoboCod): el plano `parallax_plane` (p. ej. 4) scrollea
    // a `1/parallax_div` de la velocidad del resto, leyendo su propio patrón de
    // fondo. 0xff = desactivado.
    u8 parallax_plane = 0xffu;
    u8 parallax_div = 2u;
    DirectionPolicy direction = DirectionPolicy::Bidirectional; // política de dirección
    u8 max_step = 1;               // px/frame máximos por eje de AVANCE (salto).
                                   // El algoritmo pinta cada sub-paso de 1 px ANTES de
                                   // avanzar videoposx → nunca revela píxeles sin pintar.
                                   // Un salto de N px equivale a N sub-pasos atómicos
                                   // (N columnas de tiles pintadas; coste de Blitter ∝ N).
    bool linear_display = false;       // true = display LINEAL sin split: el bitmap duplica el bucle
                                       // (espejo de filas) y el wrap se lee de forma contigua. Elimina la
                                       // limitación del split en raster 256..296 (comparador de 8 bits)
                                        // a costa de duplicar cada blit (dibujo + espejo).
    // Desplazamiento lógico de la rejilla respecto a la pista física. Con 1,1
    // la celda visible en offset (0,0) es map[0][0] y las guardas superior e
    // izquierda contienen map[-1], resuelto por el wrap del mapa.
    u16 visible_tile_bias_x = 0;
    u16 visible_tile_bias_y = 0;
};

/// Alias del caso denso/disperso (`TileLayerMap`), retrocompatible.
using XlimitedConfig = XlimitedConfigT<TileLayerMap>;

} // namespace eng::field
