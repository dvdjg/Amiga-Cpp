# Demo 201 — Mapa EHB con scroll 8-way X-Limited

Esta demo es un **tour continuo** de scroll 8-way X-Limited sobre el **mapa real EHB**
extraído de *The Fan-tasy Tileset*: barrido horizontal directo hasta el borde derecho,
luego vertical hasta el borde inferior, luego oblicuo arriba-izquierda hasta tocar de
nuevo el borde superior y, por último, Lissajous aleatorio indefinido (curvas que se
re-aleatorizan sin cortes). El **salto máximo es de 4 px/frame por eje** y todo el
recorrido es continuo, sin esperas intermedias; la franja HUD muestra la carga real de
Blitter/Copper en vivo.

El archivador del README es `main.cpp`; este documento explica el concepto del algoritmo
X-Limited, qué mecánicas del chipset Amiga explota y cómo se han generado los assets con el
pipeline de `tools/ehb` y `tools/amiga-tiles`.

---

## Tabla de contenidos

1. [El algoritmo X-Limited (XYUnLimited)](#1-el-algoritmo-x-limited-xyunlimited)
2. [Qué partes del engine usa la demo](#2-qué-partes-del-engine-usa-la-demo)
3. [Mecánicas del Amiga que se explotan](#3-mecánicas-del-amiga-que-se-explotan)
4. [Cómo se han creado los assets](#4-cómo-se-han-creado-los-assets)
5. [Cómo se invocan las tools](#5-cómo-se-invocan-las-tools)
6. [Cómo compilar, lanzar y verificar](#6-cómo-compilar-lanzar-y-verificar)

---

## 1. El algoritmo X-Limited (XYUnLimited)

El scroll de esta demo no copia píxeles a pantalla entera cada frame. Es una
implementación fiel del algoritmo **Scroller_XYLimited** de Georg Steger (documentación
en inglés en `ScrollingTricks/Docs/xylimited-uk.html`, que combina
`xlimited-uk.html` para el eje X y `yunlimited2-uk.html` para el eje Y; el mismo árbol
que documenta la demo 107).

La idea central: guardar en un **bitmap interleaved pequeño** (no una pantalla completa del
mundo) **solo la zona del mapa que rodea a la cámara**, y cada frame redibujar con el
Blitter **solo la columna/fila de tiles nueva que entra** por el lado hacia el que se
scrollea, mientras el registro `BPLxPT` del chipset se *desplaza* para que el display se
mueva sin copiar nada.

```
Mapa virtual (40x40 tiles = 640x640 px, mucho más grande que la pantalla)

   +--------------------------------------------------------------+
   |  ..  ..  ..  ..  ..  ..  ..  ..                              |
   |  ..  .X  .X  .X  .X  .X  .X  ..   ventana visible 320x~208   |
   |  ..  .X  .X  .X  .X  .X  .X  ..   (se desplaza sobre el mapa)|
   |  ..  .X  .X  .X  .X  .X  .X  ..                              |
   |  ..  ..  ..  ..  ..  ..  ..  ..                              |
   +--------------------------------------------------------------+
        ^
        '-- sólo se pinta la columna/fila que entra (∝ salto)
```

En un scroll con **cámara en movimiento libre (eje X e Y a la vez)**, el bitmap es un
*búfer rodante*: tiene más anchura que la pantalla (para el scroll horizontal) y más altura
(para el vertical), y las zonas de "relleno" que permiten el *wrap* vuelven a caer dentro
de la ventana visible al dar la vuelta. La documentación lo llama *bitmap height
dependiente del ancho del mapa*: no se duplica el mundo, solo se añade una **banda de
staging** y unas filas extra, de modo que el consumo de memoria no depende del tamaño total
del mapa.

### El scroll como sub-pasos atómicos de 1 px

El motor no hace un gran blit por salto. Cada avance de `N` px en un eje se descompone en
`N` sub-pasos de **1 px** con la política *paint-then-advance* (véase
`xlimited.hpp:update_scroll` y el comentario de `xlimited.hpp:470-473`):

- se pinta la columna (X) o fila (Y) de tiles que va a entrar,
- y **solo entonces** se avanza `videoposx/y` (lo que el Copper leerá).

Así, para cualquier salto ≤ `max_step` (16 px = la columna completa cada frame), **nunca se
muestra un píxel sin pintar**, y el coste real del Blitter crece de forma proporcional al
salto (∝ N). La demo avanza con **salto máximo de 4 px/frame por eje** (recorrido
continuo de borde a borde, sin esperas; ver §7 de `xlimited.hpp`); el `TourDriver` de `main.cpp`
recorre el mapa con la secuencia lineal *H hasta el borde → V hasta el borde → oblicuo
hasta el borde superior → Lissajous aleatorio indefinido*. La fase Lissajous ya no se
abandona nunca: cada `K_SEG_FRAMES` re-aleatoriza la órbita centrada **dentro** del mapa
(con margen = radio, para que el target nunca se clampa a un borde y la cámara no se
detenga contra la pared), y el movimiento curvo continúa sin cortes. La continuidad es
**garantizada**: como la posición está cuantizada a píxeles y la tabla Q7 tiene mesetas
en los ápices, el Lissajous descarta las muestras que no cambian de píxel y persigue la
siguiente, de modo que no existe ningún frame con `(dx,dy)=(0,0)` (cero paradas). Por
último, el HUD solo se redibuja al **cambiar de fase** (4 veces en todo el tour), nunca
en cada segmento, para que el redibujado en vblank no se note como una parada.

### La costura del *wrap* vertical (corkscrew / split)

Al scrollear en X, el desplazamiento acumulado de `videoposx` hace que la zona dibujada
avance en **planelíneas** (1 píxel de pantalla = `planes` planelíneas en memoria
interleaved). Como el DMA de bitplanes lee el bitmap en direcciones ascendentes, al llegar
al final de una fila el siguiente offset está una *planelínea* más abajo (es el "plane
shift" que describe `xlimited-uk.html`): el display continúa donde empieza la fila
siguiente, y las columnas recién pintadas quedan a medio cruce. Para no dejar basura, el
motor guarda un **`saveword`** (una planelínea de 2 bytes) al hacer blits *plane-shifted* y
lo **restaura al cambiar de dirección**, igual que el original (ver `xlimited-uk.html`
sección "saveword" y la demo `107_xlimited_corkscrew`). La escena usa además un **split
vertical** del Copper para que el display dé la vuelta dentro del propio bucle del bitmap
(banda de staging de 2 tiles).

Con `K_HUD_HEIGHT=48`, el campo mide 208 px. El split del corkscrew cae entre las líneas raster
41 y 248, dentro del rango fiable del WAIT OCS; por eso se usa `linear_display=false` y no se
duplica el bucle físico. El HUD ocupa las 48 líneas inferiores mediante la zona overlay del Copper.

### El borde del mapa y la pista de pre-pintado

El bitmap x-limited tiene `bitmap_blocks_per_row` bloques más de ancho que la ventana (22 en
esta demo): la zona "extra" es la **pista de staging** por la que entran las columnas nuevas
antes de `videoposx` revelarlas. El origen de cada blit es `mapblockx + 22`, y al acercarse al
final del mapa ese origen puede apuntar **a una columna que ya no existe** (mapa 40×40 →
`mapx = 40..61`), incluso con el avance de la cámara clavado en el límite. Sin protección, el
motor habría leído `edge_tile` (0 en esta demo) y pintado el tile 0 real del banco en la pista
de staging, que la ventana muestra junto al borde derecho/inferior.

En esta demo el mapa es toroidal (`wrap_x=40`, `wrap_y=40`), por lo que un origen fuera de la
rejilla se resuelve mediante el lado opuesto: `mapx=40` lee la columna 0 y `mapy=40` lee la fila
0. La pista de pre-pintado puede cruzar ambos bordes sin introducir `edge_tile`. La verificación
host está en `tools/analyze/verify-201-border.mjs`.

### Cómo se monta el framebuffer y el contrato de *qué* se ve

El engine no copia el mapa a pantalla entera: mantiene un **anillo** interleaved
(`bitmap_blocks_per_row` × `display_blocks_per_col` bloques) que se monta en dos fases y
luego solo se pinta lo que entra:

- **Fase A — SETUP (relleno):** `XlimitedScene::fill` pinta el anillo entero con el mapa
  empezando una celda física antes del offset lógico; esa celda es la guarda toroidal superior
  e izquierda. Es un blit por bloque, en lotes (≤120 jobs por plan).
- **Fase B — INCREMENTAL (scroll):** `XlimitedScene::update(plan, dx, dy, frame)`
  descompone el avance en **sub-pasos atómicos de 1 px** (`update_scroll`) y pinta la
  columna/fila entrante **solo al cruzar** cada límite de 16 px (`scroll_right/left/down/up`),
  con el ajuste de *fillup* al completarse el bloque.

**Confirmación del reparto de carga (así sigue siendo):** como cada sub-paso es de 1 px, en un
desplazamiento de `N` px por eje se pinta la columna/fila nueva en los `N` sub-pasos, y el
*Blitter* ejecuta un único blit interleaved (todos los planos) por bloque. Al avanzar 16 px ya
está dibujada la fila/columna completa que entra; el coste por frame crece **proporcional al
salto** (∝ N), no al tamaño de la pantalla. Con el salto máximo de 4 px/frame por eje de la
demo, a lo sumo hay 1 cruce de bloque por eje y frame (dentro del presupuesto de Blitter,
`max_step` configurable a 16).

**El contrato de qué mundo se ve (para diagnósticos y objetos fijos):** el engine garantiza el
invariante de Steger `display_start == scroll_x`, de modo que la pantalla muestra el mundo según
`screen_to_world_x/y`:

- `world_x(sx) = offset_x + sx` (columnas de mundo).
- `world_y(sy) = offset_y + sy` (filas de mundo).

Ojo con dos consecuencias que suelen confundir al comparar con una imagen de referencia:

1. **Guardas físicas:** el hardware XY usa dos filas/columnas de staging, pero la API de escena
   expone coordenadas lógicas sencillas. Con `visible_tile_bias_x=1` y `visible_tile_bias_y=1`,
   `(offset_x,offset_y)=(0,0)` muestra `map[0][0]`; las guardas contienen `map[-1]`, resuelto por el toroide.
2. **Toroide lógico:** al subir desde `offset_y=0` se muestra la fila 39; al avanzar desde
   `offset_x=320`, la siguiente columna visible es la 0. El módulo físico del anillo no se usa
   como índice lógico del mapa.

La herramienta host que modela este contrato (y dice **qué tiles deberían verse** para un
offset arbitrario, recortando la referencia) es `tools/analyze/verify-201-framebuffer.mjs`
(`--cam x,y`). Para **arrancar en un offset distinto de (0,0)** (p. ej. en el bloque
`(100,100)`), la receta es `scene.fill(...)` (setup del anillo) y después
`scene.pre_scroll(backend, plan, 100, 100)` (reutiliza el scroll incremental para mover la
cámara sin re-pintar); también se ve en el modo `--origin` de la herramienta.

---

## 2. Qué partes del engine usa la demo

El `main.cpp` es un **consumidor fino**: la mayor parte de la lógica vive en el engine.
Las inclusiones y su uso real:

| Archivo del engine | Qué aporta a la demo |
|---|---|
| `engine.hpp` | El bucle principal `update → wait_vblank → render` (`render` es el punto de commit; el HUD se redibuja ahí, durante el vblank, para no competir con el DMA en scanlines visibles). |
| `field/xlimited_scene.hpp` | **`XlimitedScene<kScrollConsts>`**: orquesta `begin`/`fill`/`update`/`compose`/`install` sobre el playfield X-Limited, incluye la franja HUD (`hud_surface`) y el campo de scroll. |
| `field/xlimited.hpp` | El corazón del algoritmo: `XLimitedPlayfield` + `ScrollEngine` (scroll 8-way, `update_scroll`, `draw_block_job`, `add_draw`, `saveword`). |
| `field/playfield.hpp` | Base `Playfield` con las primitivas de dibujo CPU (`set_pixel`/`fill_rect`) que usa el HUD. |
| `graphics/frame_plan.hpp` | Cola de **blits** del frame (`FramePlan::add_tile_block_copy`, presupuesto y telemetría). |
| `graphics/surface.hpp` | `Surface` para el lienzo del HUD. |
| `memory/arena.hpp` | `MemorySystem`/arena de Chip RAM (200 KB para display+copper+relleno; el banco incbinado se alia sin copiar). |
| `platform/amiga_minimal.hpp` | Backend mínimo: configura la memoria Chip, ejecuta el plan de frame, instala la copperlist y el display. |
| `debug/run_status.hpp` | `g_eng_run_status` (estado READY/FAILED por el canal lateral) y `g_eng_frame_telemetry` (jobs/words/copper por frame). |
| `core/sinetable.hpp` | `SineTable<127,256>` a priori (Q7) para las trayectorias circular y Lissajous, sin float por frame. |

La demo añade por su cuenta: `TourDriver` (conductor de la secuencia lineal de fases) y el
mini-fuente 5×7 para el HUD.

---

## 3. Mecánicas del Amiga que se explotan

**Modo EHB (Half-Bright, planos 1-6).** Con `BPLCON4 = 1` y 6 bitplanes, el chipset genera
automáticamente los colores *half* `32..63` como `base/2` por componente de los 32 bases
(`COLOR00..31`). Por eso la demo carga **solo 32 registros de color** (`kEhbPalette` toma los
32 bases; `g_palette[32]`) y el hardware fabrica los 32 oscuros gratis. El índice del tile
se interpreta directamente: el bit 5 del índice EHB enciende el plano 6 / half
(`main.cpp:118`, `xlimited_scene.hpp` conversión índice→planos). Esto dobla la paleta
"percibida" a 64 sin gastar planos extra ni CPU.

**Scroll horizontal y vertical acelerado por hardware.** El desplazamiento fino lo hace el
chipset, no la CPU:
- el **scroll horizontal** se programa con `BPLCON1` (valor de scroll fino) y moviendo los
  punteros `BPLxPT` (el avance grueso de 16 px) — la CPU solo escribe 2 registros por frame,
- el **scroll vertical** y el *wrap* del búfer rodante se logran moviendo los punteros de
  plano (`BPLxPT`) con el split, de nuevo sin copiar la pantalla.

La CPU *no* dibuja cada frame la pantalla: calcula las coordenadas y programa registros
(tan barato como en los juegos reales de la época, p. ej. Mega Typhoon). El coste real del
frame está en pintar con Blitter las columnas/filas nuevas, no en el display.

**Copper (copperlist programada con zonas).** Una sola copperlist por frame:
- la **zona main** mantiene `BPLCON4=1` (EHB on) y los 6 `BPLxPT` al campo de scroll,
- en la **zona HUD** (`hud_raster`) se apaga el EHB (`BPLCON4=0`) y se apuntan los planos
  al lienzo del HUD, con su propia paleta.

Esto permite un HUD de texto con colores planos distintos del mapa, recortado de la misma
copperlist en la parte baja de la pantalla (split vertical). Ver la verificación del dump
real del Copper en `docs/roadmap/XLIMITED_8WAY_EHB_201.md`.

```
             BPLCON4   planos        contenido
screen  ┌──────┬──────────┬──────────────────┐
  main  │ 1    │ 1..6     │ mapa EHB (scroll)│
        ├──────┼──────────┼──────────────────┤
  HUD   │ 0    │ 1..6     │ telemetría texto │
        └──────┴──────────┴──────────────────┘
```

**Blitter (DMA de copia de bloques).** Cada tile es un único `BlitJob` interleaved
(`draw_block_job` en `xlimited.hpp`): un blit de `BLOCKPLANELINES` líneas que copia **todos
los planos a la vez** porque el bitmap está interleaved (`plane_stride = words`, módulo
`dst_mod`), en lugar de `planes` blits separados. El módulo de filas del blitter compensa el
salto de fila, y la DMA del Blitter hace la copia sin tocar la CPU. La telemetría
`g_eng_frame_telemetry` (jobs/words/copper) permite comprobar que el presupuesto no se
desborda en el peor caso (paso 16, columna entera por frame).

**Chip RAM interleaved (BMF_INTERLEAVED).** El bitmap se reserva interleaved por
planelínea (fila × 6 planos contiguos), lo que hace el scroll horizontal "plane-shifted"
sea barato y el Blitter pueda copiar todos los planos de un tile con un módulo simple. El
banco de bloques interleaved incbinado se **alia sin copia** (no se reserva una segunda
copia), de modo que todo quepa en los 512 KB de Chip RAM de un A500.

---

## 4. Cómo se han creado los assets

Los activos *no* son binarios dibujados a mano: salen de un pipeline de herramientas Node en
`tools/` que convierte la imagen original en datos listos para incbinar en el Amiga. El
punto de partida es el atlas **`tools/amiga-tiles/assets/Beginning Fields.png`**
(640×640 = 40×40 tiles de 16×16, sacado de *The Fan-tasy Tileset*).

El flujo completo y los ficheros que produce:

```
 Beginning Fields.png (640x640, 40x40 tiles 16x16)
        │  ①  quantize-ehb.mjs          (elige 32 bases EHB half-aware)
        ▼
 out/ehb/palette.json                   (32 bases RGB) + ehb_palette.h + preview
        │  ②  slice-tiles.mjs           (cuantiza, dedupe 1149 tiles, reindexa BASES-PRIMERO)
        ▼
 out/ehb/tilebank.raw.bin               (índices EHB 0..63, 256 B/tile, 1149 tiles)
 out/ehb/tilebank_indexed.h             (paleta + mapa kTileIndexedMap + stride)
 out/ehb/tiles.json                     (metadatos: banco, mapa, stats)
 out/ehb/reconstruct.png / tilebank.png (verificación host)
        │  ③  emit-const-201.mjs        (renombra .h -> const_game_201.h)
        ▼
 out/ehb/const_game_201.h               (kEhbPalette[64] + kRenderMap[1600])
        │  ④  emit-xlimited-bank.mjs    (índices -> banco interleaved 320px)
        ▼
 out/ehb/tilebank.xlimited.bin          (222,720 B, 6 planos interleaved, listo)
```

La demo incrusta:
- `const_game_201.h` mediante `#include "../../../out/ehb/const_game_201.h"` → paleta
  `kEhbPalette` y mapa `kRenderMap` (los 40×40 índices al banco),
- `out/ehb/tilebank.xlimited.bin` mediante `incbin` en la sección `tiles.MEMF_CHIP`
  (hunk `HUNKF_CHIP`), para que `LoadSeg` lo cargue en Chip RAM y la escena lo **alia sin
  copia** (`blocks_prebuilt`).

### Por qué el banco interleaved se construye en el host y no en el Amiga

Convertir el tilebank *indexado* (287 KB) a *interleaved* (222 KB) en el Amiga exigiría
cargar ambos **más** el display en la Chip RAM de 512 KB del A500, y pagar la conversión
por píxel en CPU en cada init. Haciéndolo **en el host**:
- el Amiga solo incbina el banco interleaved (222 KB) y lo alia sin copiar,
- el scroll no paga ninguna transformación por píxel (regla de oro del pipeline: el Amiga
  no debe reindexar/convertir píxeles nunca).

Esto es lo que `emit-xlimited-bank.mjs` haría idénticamente en el motor
(`xlimited_build_blocks_bitmap_from_indexed`, `xlimited_scene.hpp`), pero fuera del Amiga.

### La convención de índices BASES-PRIMERO

`slice-tiles.mjs` trabaja internamente con una paleta *intercalada* `[base0,half0,
base1,half1,…]` para emparejar cada base con su half al cuantizar. Pero el **chipset EHB
consume bases-primero** (`COLOR00..31` = bases, `32..63` = half → bit 5 = plano 6). Para que
el Amiga **no transforme píxeles**, `slice-tiles.mjs` reindexa en el propio export
(`ehb-export-map.mjs`, `buildExport`) aplicando la biyección
`e = (v>>1) | ((v&1)<<5)` sobre los índices y a la paleta, dejándolos listos para el
chipset. Ver la regla 7 de `docs/roadmap/REGLAS_PIPELINE_TILES.md`.

---

## 5. Cómo se invocan las tools

### Cuantizar la imagen → paleta EHB

```bash
node tools/ehb/quantize-ehb.mjs tools/amiga-tiles/assets/Beginning\ Fields.png --out out/ehb
```

Detecta el número de colores únicos del original y elige automáticamente el modo: con >32
colores usa **6 planos EHB** (32 bases, `half(c)=c>>1` cada componente; el blanco `$FFF`
aporta su half `$777` gratis). Reserva el color 0 para transparencia y emite
`out/ehb/palette.json` (32 bases), `ehb_palette.h` (palabras Amiga `0x0RGB`) y
`ehb_preview.png` con el error MSE/PSNR del remapeo {base,half}. Se puede forzar el modo con
`--force ehb` / `--force 4` / `--force 5`.

### Cortar en tiles y dedupe → banco indexado

```bash
node tools/ehb/slice-tiles.mjs tools/amiga-tiles/assets/Beginning\ Fields.png \
  --palette out/ehb/palette.json \
  --out out/ehb
```

Cuantiza el original a la paleta EHB, corta en tiles 16×16, elimina duplicados exactos
(dedupe), reindexa a **bases-primero** y emite:
- `tilebank.raw.bin` (índices EHB 0..63, 256 B/tile, stride fijo) — el binario incrustable,
- `tilebank_indexed.h` (paleta `kTileIndexedPalette`, mapa `kTileIndexedMap`, stride/bytes),
- `tiles.json` (metadatos del banco y del mapa),
- `reconstruct.png` + `tilebank.png` (verificación: la reconstrucción del banco debe dar
  exactamente el 100 % sin fusión; assert que aborta si no cuadra).

Opciones: `--ehb-merge 0..1` para fusionar tiles similares (con pérdida, reduce el banco),
`--sheet-scale`, `--classify` (describe el banco con Ollama).

### Renombrar el `.h` a los nombres de la demo

```bash
node tools/ehb/emit-const-201.mjs --in out/ehb/tilebank_indexed.h \
  --json out/ehb/tiles.json --out out/ehb/const_game_201.h
```

Genera `const_game_201.h` con `kEhbPalette[192]` (64 colores RGB) y `kRenderMap[1600]`
(el mapa 40×40 de índices al banco) que `main.cpp` incluye.

### Convertir el banco indexado → banco interleaved X-Limited

```bash
node tools/ehb/emit-xlimited-bank.mjs \
  --in out/ehb/tilebank.raw.bin \
  --out-bin out/ehb/tilebank.xlimited.bin \
  --out-h out/ehb/tilebank.xlimited.h
```

Convierte los índices a los 6 bitplanes interleaved en un bitmap de 320 px de ancho
(40 B/planelínea), colocando cada tile en `(tile % 20, tile / 20)`. Emite
`tilebank.xlimited.bin` (el que la demo incbina) y la cabecera `g_tilebank_xlimited[_size]`.

> **Nota sobre resultados (evidencia, 2026-09):** con el atlas *Beginning Fields* el
> pipeline produce **1149 tiles únicos** de 16×16, un mapa **40×40** (`tiles.json`:
> `unique=1149`, `cols=40`, `rows=40`; `const_game_201.h`: `kRenderMap[1600]`) y un banco
> interleaved de **222,720 B** (`tilebank.xlimited.bin`). **Verificado por regeneración**:
> ejecutar las cuatro invocaciones de esta sección sobre `Beginning Fields.png` produce
> `tilebank.raw.bin`, `tilebank.xlimited.bin` y `const_game_201.h` **byte-idénticos** (SHA-256)
> a los commiteados en `out/ehb/`, y `slice-tiles.mjs` reporta `COMPARAR=100.00%` y
> `roundtrip=0` — así que estas instrucciones reproducen exactamente lo que la demo incrusta.
> Estos números se corresponden con la configuración compilada en `main.cpp`
> (`kTileCount=1149`, `kMapW=kMapH=40`) y con la inclusión de `tilebank.xlimited.bin`. Si
> regeneras los assets con otro atlas o con `--ehb-merge < 1`, recuerda **recompilar la demo**
> con los nuevos valores de `kTileCount`/mapa o la reconstrucción fallará.

### Variante todo-en-uno (tools/amiga-tiles)

`tools/amiga-tiles/amiga-tiles.mjs` generaliza el mismo pipeline a 2..255 colores (EHB =
64), con dithering, redimensionado y export del banco interleaved X-Limited (`--xlimited`).
Es la vía recomendada para preparar assets de otros juegos. Ejemplo:

```bash
node tools/amiga-tiles/amiga-tiles.mjs tools/amiga-tiles/assets/Beginning\ Fields.png \
  --out out/mi_map
```

Los resultados de `out/tile-demos/07_tiles_32x32/atlas_32x32` muestran ese banco generado a
32×32: `tilebank_64c_ehb_none_640x640.bin` (409,600 B = 400 tiles únicos × 1024 B/tile,
misma imagen 640×640 rebanada con tiles de 32 px) puede cargarse directamente en el Amiga
como binario.

---

## 6. Cómo compilar, lanzar y verificar

```bash
bash ./tools/build/build-demo.sh demos/201_ehb_map --debug --clean
bash ./tools/run/run-demo.sh demos/201_ehb_map [--sequence-frames N]
bash ./tools/analyze/analyze-demo.sh demos/201_ehb_map
```

Parámetros de compilación (`EXTRA_DEFINES="-D…"`):
- `K_VIEWPORT_H` (256) alto total (incluye la franja HUD). Con HUD, el comparador de 8 bits
  del Copper limita `main_h = viewport_h - hud ≤ 214`; con `hud=48` queda `main=208`.
- `K_HUD_HEIGHT` (48) franja inferior con la telemetría en vivo.
- `K_SEG_FRAMES` (150) frames por segmento (~3 s a 50 fps).
- `K_START_X` / `K_START_Y` (0) **origen del juego en el mapa** (píxeles de mapa). Con valor
  distinto de 0, la demo hace un `pre_scroll` tras el `fill` (pinta incrementalmente las
  columnas/filas entrantes) para que la **1ª imagen** se muestre en esa coordenada. Sirve para
  ubicar el origen de un juego en un punto arbitrario de un mapa grande (la rejilla de índices
  es independiente del tamaño del tilebank). Ojo: el scroll vertical envuelve cada
  `display_height` (288 px, ver §1) por el anillo, así que para mapas mucho más altos hay que validar el
  wrap vertical (ver §1).
- `K_FB_SELFCHECK` (0) activa un verificador EXPERIMENTAL de framebuffer (lee el anillo y lo
  compara con `kRenderMap`). **Dejar 0 en release**: su mapeo mundo→dirección no coincide con el
  *walk* del chip y da falsos positivos (ver §1); no debe activarse para producción.

La demo expone `g_eng_run_status` (READY por canal lateral 127.0.0.1:2346) y
`g_eng_frame_telemetry` (blit_jobs/blit_words/copper_words por frame) para que el runner
verifique 50 fps y el presupuesto. En el emulador WinUAE-DBG va ~48 fps; en hardware real
va a 50 fps.


## 7. Lecciones de implementación (checklist para no repetir errores)

Esta sección condensa lo aprendido al portar y depurar el algoritmo XYLimited/corkscrew de
Georg Steger al engine C++23 sobre Amiga. Son los invariantes NO obvios que, si se tocan sin
entenderlos, rompen la imagen de formas que parecen "fallos al azar". Léela completa antes de
modificar el scroll de cualquier demo corkscrew (101/107/201).

### 7.1 El anillo vertical (display_height) es INDEPENDIENTE del HUD — y debe ser 288, no 240

El corkscrew mantiene un **anillo vertical** de `display_height` píxeles que el display recorre.
El original de Steger usa `BITMAPHEIGHT = SCREENHEIGHT + 2*BLOCKHEIGHT` = `256 + 32 = 288`
(18 bloques). El scroll horizontal dibuja la columna entrante con `mapy` de 1 a 17 (por el
plane-shift "diagonal"), y calcula `y = (block_videoposy + mapy*16) % display_height`.

Si `display_height` es 240 (por restar el HUD: `208 + 32`), entonces `mapy=16 → y=256 % 240 = 16`
colisiona con `mapy=1 → y=16`, y `mapy=17` con `mapy=2`: el tile de la fila 16/17 **sobrescribe**
el de la fila 1/2 en la banda de staging, y el display muestra arriba las filas que deben ir
abajo. Síntoma: "tres filas de tiles en la parte superior incorrectas, que deberían estar abajo".

Regla: el **HUD recorta el área VISIBLE** (`viewport_h` del campo = 208) pero **NO el anillo**.
El anillo se dimensiona para el viewport TOTAL: `display_height = 256 + 2*16 = 288`. El engine
lo expone como `cfg.display_height` en `XlimitedConfig` (0 = auto desde `viewport_h`); la escena
lo fija a `cfg.viewport_h + 2*tile_height`. Las constantes NTTP `ScrollConsts.display_height`
deben coincidir (validación en `begin()`).

```
        anillo (288 = 256+32)          HUD (48) overlay
  ┌──────────────────────────────┐     ┌──────────────────┐
  │  staging superior (16 px)    │     │   visible main   │ ← viewport_h del campo = 208
  │  visible main 208 px         │  =  │   208 px (13 bl.) │   (el HUD oculta 208..255)
  │  staging inferior + HUD 64px │     └──────────────────┘
  └──────────────────────────────┘
```

### 7.2 block_videoposy envuelve en display_height, NUNCA en bitmap_height

`block_videoposy` es la fila de bloque donde se pre-pinta la banda entrante. Debe envolver en el
**bucle del display** (`display_height`, 288), porque `bitmap_height` (304) incluye las filas
extra del *walk* horizontal (planeaddx). Envolver en `bitmap_height` dibuja la banda entrante en
las filas extra que el display SÍ muestra al caminar en X → píxeles basura por toda la pantalla
desde el primer frame. Es un fix documentado "Corregido 2026-08-31: envolver en display_height";
no volver a cambiarlo a `bitmap_height`.

### 7.3 El offset visible (-16, -16) y el `visible_tile_bias`

El hardware XYLimited **esconde los primeros 16 px** de cada eje: el display lee el anillo con un
desfase de 1 bloque (vertical, `display_offset = videoposy + 16`) y el fetch de DDFSTRT=$30 se
adelanta 16 px (horizontal). Sin corrección, `map[0][0]` nunca se ve y, al hacer scroll de 320 px
(media vuelta de un mapa 40x40), aparece la columna 0 a la derecha: parece un offset (-16,-16).

El `visible_tile_bias (1,1)` desplaza el CONTENIDO del anillo 1 celda: `fill` y `scroll` pintan
`map[a-1][b-1]` en la celda física `(a,b)`, de modo que en el offset (0,0) la esquina visible es
`map[0][0]`. El contrato pasa a ser `world_x = mapposx + sx`, `world_y = mapposy + sy` (SIN +16).

Clave para no romperlo: los `mapx/mapy` del scroll (`mapblockx + BPR`, `mapblocky + BPC`, etc.)
son **celdas físicas del anillo**, NO índices lógicos del mapa. Por eso `map_tile_at(px,py) =
tile_at(px - bias, py - bias)` es CORRECTO para el fill Y para el scroll: ambos usan el mismo
convenio de celdas. Si alguien "corrige" el scroll restándole bias dos veces, o revierte el bias
"por si acaso", reintroduce el offset (-16,-16). El fallo real que rompía la demo con bias=1 era
el de §7.1/§7.2, no el bias.

### 7.4 La geometría interleaved y el walk plane-shifted

- El bitmap es interleaved: `planeline = pixel_row*planes + plane`, cada planelínea de 44 B
  (352 px) con 6 planos → 1 scanline = 264 B. Un tile 16x16 es 96 planelíneas y se copia con UN
  solo blit (`BLTSIZE = 96*64 + 1`, src mod 38, dst mod 42).
- El scroll horizontal hace un *walk*: `planeaddx` crece 2 B por cada 16 px y el puntero BPLxPT
  avanza; la columna entrante se dibuja **plane-shifted** en `x = x0 + bitmap_width` (x_word=44 →
  1 planelínea más abajo). El `saveword` guarda la planelínea que pisa ese blit y se restaura al
  invertir la dirección.
- `bitmap_height` debe dar cabida al walk (formula `display_height + (map_width/BPR/planes)+1+3`,
  redondeado a múltiplo de 16 = 304 en esta demo). El banco incbinado (222 KB) se alia sin copia.

### 7.5 Mapa toroidal y staging

Con `wrap_x = map.width` y `wrap_y = map.height`, `tile_at` resuelve por módulo (no hay
`edge_tile`). La pista de staging (22 celdas por delante) lee columnas/filas que envuelven al
lado opuesto; así el scroll puede cruzar el borde sin pintar tiles falsos. Para mapas no
toroidales (`wrap=0`) el `add_draw` clampa el origen a la última columna/fila.

### 7.6 El split del corkscrew y el Copper

El display envuelve el anillo en `split_line = display_height - display_offset`; el Copper
reinicia los punteros BPLxPT a la fila 0 en ese raster. El comparador WAIT del Copper es de 8
bits (raster <= 255), así que el split solo es fiable si `split_line + 41 <= 255`. Con
`display_height=288` y vista de 208 px, el split activo cae dentro de la ventana y es correcto.

### 7.7 EHB (6 planos) — detalles que no tocar

- `BPLCON4 = 1` activa Half-Bright con 6 planos: solo se cargan 32 registros de color base
  (COLOR00..31) y el hardware fabrica los half (32..63) como base/2.
- Convención BASES-PRIMERO: el índice EHB 0..63 ya es el número de color; bit 5 = plano 6 = half.
- Al cambiar de playfield (zona HUD) hay que reprogramar BPLCON0 con el nº de planos del HUD y
  `BPLCON4 = 0` (sin EHB), o el bit half/EHB mezcla colores en la franja.

### 7.8 Orden de verificación de cualquier cambio

1. Build + run con `--warp` y comparar varios frames contra `reconstruct.png` recortado al
   contrato (`world_x = mapposx+sx`, `world_y = mapposy+sy`): esperado ~5-7 % (cuantización EHB).
2. Comprobar el ORIGEN: `frame_000` debe tener la esquina `map[0][0]` arriba-izquierda.
3. Comprobar scroll horizontal (fin de fase 1), vertical (fase 2) y diagonal: ~5-7 % en todos.
4. No tocar a la vez: anillo (`display_height`), `block_videoposy`, y `visible_tile_bias`. Son
   tres invariantes independientes; cambiar uno sin los otros produce los síntomas de §7.1-§7.3.
