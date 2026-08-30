# 107_xlimited_corkscrew

Demo del algoritmo **XYLimited / corkscrew** de Georg Steger (ScrollingTricks):
scroll **8-way** con el truco X-Limited extendido a vertical, sin el ruido del
controlador circular de `TileFieldController`.

## Qué demuestra

- Un campo **interleaved** (`BMF_INTERLEAVED`) de 352×304 (ó 384 con fetch
  ancho) que hace scroll **horizontal y vertical infinito** con un mapa de
  320×256 tiles de 16×16 sin tearing.
- **Bucle vertical de display** `display_height = viewport_h + 2*tile_height`
  (288 para 320×256): el display envuelve al llegar al final y un **split de
  Copper** vuelve a la fila 0. La fila/columna entrante se **pre-pinta en la
  banda de staging** de 2 bloques (`block_videoposy`, `y = block_videoposy*planes`)
  que el display alcanza al dar la vuelta — el invariante "corkscrew" del original.
- Cada píxel es como máximo 1-2 blits de `BLOCKPLANELINES` líneas
  (`16*planes = 64` para 4 planos). El `x` de la columna entrante es
  plane-shifted a la derecha (`BITMAPWIDTH + ROUND2BLOCKWIDTH(videoposx)`) y el
  `y` de la fila entrante usa `(block_videoposy + mapy*tile_height) % display_height`.
- La guarda de 1 word (`saveword`/`savewordpointer`) evita el artefacto de
  2 bytes al invertir la dirección, igual que en `xlimited.c`/`XYLimited`.
- `TWOBLOCKSTEP = bitmap_blocks_per_row - tile_height` (22-16=6 para 352 px):
  2 bloques por paso mientras `stepy < TWOBLOCKSTEP`, 1 después, para cubrir la
  fila completa en 16 píxeles de scroll vertical.

La demo es deliberadamente **single playfield de 4 planos** (16 colores) para ver
el corkscrew puro. No hay `surface_origin` ni bandas del modelo circular: el
posicionamiento físico es `frontbuffer + y*BITMAPBYTESPERROW + x` con `y` en
planelíneas.

## Ciclo de fases

El scroll avanza 1 px/frame en un ciclo de 4 fases de 1000 frames (~20 s a 50 fps):

| Fase | Movimiento |
|---|---|
| 0 | Horizontal derecha |
| 1 | Vertical abajo |
| 2 | HV alternando (derecha/abajo) |
| 3 | Diagonal Lissajous (sin(x), cos(0.7x)) |

Todas usan el port corkscrew (XYLimited) de `xlimited.hpp` (§ Scroll corkscrew),
verificado bloque a bloque contra `Scroller_XYLimited/main.c`.

## Parámetros de compilación

| Macro | Valores | Efecto |
|---|---|---|
| `K_TILE_WIDTH` | `16`, `32` | Anchura de tile en píxeles (múltiplo de 16). 32 usa 2 words por fila y `BITMAPBLOCKSPERROW` ajustado. Con `32` la demo fuerza `BITMAPWIDTH=384` (`BLOCKSPERROW=24`) para mantener el contrato entero. |
| `K_FETCH_MODE` | `0`, `1`, `3` | Modo de fetch del Agnus. `0`=16 px normal (352 px, `DDFSTRT=$30`/`$D0`, `bitmapoffset 0`, `modulo 2`, scroll 16). `1`=BPL32 32 px (384 px, `DDFSTRT=$28`/`$C8`, `bitmapoffset 16`, `modulo 4`, scroll 32). `3`=BPL32+BPAGEM 64 px (384 px, `DDFSTRT=$18`/`$B8`, `bitmapoffset 48`, `modulo 8`, scroll 64). Cuando `K_FETCH_MODE !=0` la demo fuerza `BITMAPWIDTH=384` y `fetch_mode=K_FETCH_MODE` independientemente de `K_TILE_WIDTH`. |
| `K_PLANES` | `4` (defecto) | Profundidad. 4 es el caso canónico de X-Limited; otros valores sólo para pruebas de altura extra. |

`BITMAPWIDTH` es `352` (`BLOCKSPERROW=22`, `44 bytes` por planelínea) en modo normal y `384` (`BLOCKSPERROW=24`, `48 bytes`) con tiles de `32` o con fetch ancho. `BITMAPBYTESPERROW` es `44` ó `48` y `BPL1MOD/BPL2MOD = BITMAPBYTESPERROW*planes - 40 - moduloOffset` (2, 4 ú 8 según el modo).

`BPLCON1` replica el desplazamiento fino en ambos nibbles (`(fine &15)*0x11`) y añade los bits de fetch ancho: `fine &16 → 0x4400`, `fine &32 → 0x8800` (ver `xlimited.c:304-311` y `engine/include/eng/field/xlimited.hpp §4`).

```bash
# 352 píxeles, tiles 16, 4 planos (modo por defecto, fetch 16 px)
bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# Tiles de 32 píxeles (misma altura, doble anchura, fuerza 384)
bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean -DK_TILE_WIDTH=32

# Fetch ancho 32 px (BPL32, 384 px, DDF $28/$C8, offset 16, scroll 32)
bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean -DK_FETCH_MODE=1
EXTRA_DEFINES="-DK_FETCH_MODE=1" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# Fetch ancho 32 px + tiles 32 px (384 px, 24 bloques por fila)
EXTRA_DEFINES="-DK_TILE_WIDTH=32 -DK_FETCH_MODE=1" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# Fetch ancho 64 px (BPL32+BPAGEM, 384 px, DDF $18/$B8, offset 48, scroll 64)
EXTRA_DEFINES="-DK_FETCH_MODE=3" bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean
```

## Verificación

- **Test host**: `node tools/analyze/verify-xlimited.mjs` modela direcciones
  lineales, fetch contiguo, altura extra `viewport_h+EXTRAHEIGHT? + (map_width/blocksPerRow/planes)+1+3`,
  guarda de 1 word y la geometría del corkscrew (§11: `display_height`,
  `BITMAPBLOCKSPERCOL`, `TWOBLOCKSTEP`, `block_videoposy`, split).
- **Port vs original**: `node tools/analyze/verify-corkscrew.mjs` (test host)
  compara **bloque a bloque** los 4 scrolls del port (`XlimitedField`) contra una
  réplica fiel de `Scroller_XYLimited/main.c` para el config de la demo
  (secuencias D/U/R/L, XY mezclado y 5000 pasos aleatorios). Debe imprimir `OK`.
- **Análisis visual**: `bash ./tools/analyze/analyze-demo.sh demos/107_xlimited_corkscrew`
  o `analyze-sequence.sh` si existe.
- **Secuencia corkscrew** (`demos/107_xlimited_corkscrew/analyze-sequence.sh`):
  valida 1) scroll continuo a la derecha 100 frames, 2) inversión brusca a la
  izquierda 1 frame sin hueco de 2 bytes (`saveword`/`savewordpointer`), 3)
  fila/columna entrante en la banda de staging y split vertical.

  ```bash
  # Secuencia completa (requiere WinUAE-DBG; no asume toolchain en PATH salvo AMIGA_BIN_PATH)
  bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean
  bash ./demos/107_xlimited_corkscrew/analyze-sequence.sh --warp
  # Sin warp (tiempo real, para evaluar suavidad)
  bash ./demos/107_xlimited_corkscrew/analyze-sequence.sh
  ```

  El flag `--warp` propaga `warp=true` a WinUAE vía `tools/run/run-demo.sh`
  (usa `AMIGA_BIN_PATH` como fallback del toolchain, igual que `build-demo.sh`).
  En `tools/test-regression.sh` la regresión invoca automáticamente
  `analyze-sequence.sh --warp` cuando existe.

## Evidencia runtime (WinUAE-DBG, 2026-08-31)

Verificado el **scroll vertical** (fase V, cámara bajando) con captura de 60
frames a 20 ms y análisis determinista + visión local (qwen3-vl):

- **Sin banda negra al pie**: 0 % de negro en la fila inferior e interna del
  viewport (el bug original dejaba la fila entrante en negro/basura).
- **Contenido desplazándose arriba ~1 px/frame** (mediana −1 px nativo), que es
  el signo correcto de una cámara bajando (`scroll_down`).
- **Tiles bien formados, sin tearing** (qwen3-vl: "no hay filas negras ni
  tearing, los tiles se ven bien formados").
- La fase H (derecha) pasa `analyze-sequence.sh` completa (100 frames,
  `ChangedPairs=99`, `DuplicatePairs=0`, telemetría mapposx/videoposx/BPLCON1).

## Limitación OCS conocida

El encoder de WAIT de Copper del engine cubre líneas 0..255. El split vertical
del corkscrew puede caer en las líneas 256..296 (cuando
`display_offset < 74`); en ese caso se recorta a la línea 255, mostrando la
banda inferior (1..41 filas) con el wrap adelantado unos píxeles. Confirmado en
emulador: la banda es visualmente sutil (contiene tiles de las filas extra, no
negro ni tearing). Afecta solo a las fases verticales en ~16% de los frames.
Para eliminarlo hace falta un WAIT de 9 bits en `copper.hpp` (verificar contra
WinUAE-DBG antes de aplicar).

## Arquitectura

- `engine/include/eng/field/xlimited.hpp` — `XlimitedField` + `XlimitedDisplayComposer`.
  Port del corkscrew (XYLimited): `scroll_right/left/up/down` fieles a
  `Scroller_XYLimited/main.c`, banda de staging `block_videoposy`, split
  vertical en `display_height`, paleta al inicio del frame. Allocation
  `BMF_INTERLEAVED-like`: `row_bytes*planes*height`, addressing
  `frontbuffer + y*BITMAPBYTESPERROW + x`.
- `demos/107_xlimited_corkscrew/src/main.cpp` — usa `MinimalBackend`,
  `g_eng_run_status` y `demo::pf_plane_row`/`cell_hash` para construir el banco
  de bloques en el layout clásico de Steger (320×256 interleaved, tiles en
  `(tile%20, tile/20)`) y el mapa; no reutiliza la lógica circular
  (`surface_origin`, recentrado, bandas).

Referencias: `ScrollingTricks/Docs/xlimited-uk.html`,
`amiga-stuff/scrolling_tricks/xlimited.c`,
`amiga-stuff/scrolling_tricks/xylimited.c` (corkscrew),
`docs/architecture/AMIGA_8WAY_SCROLLING.md` (§3 errata, §7.4, §11, §13).
