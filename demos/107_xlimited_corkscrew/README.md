# 107_xlimited_corkscrew

Demo limpia del algoritmo **X-Limited** de Georg Steger (ScrollingTricks) sin
ruido del controlador circular de `TileFieldController`.

## Qué demuestra

- Un campo **interleaved** (`BMF_INTERLEAVED`) de 352×268 (ó 384×267 con fetch
  ancho) que hace scroll **horizontal infinito** con un mapa de 256×128 tiles
  de 16×16 sin tearing y sin Copper segmentado.
- La columna entrante se dibuja con **un único blit** de `BLOCKPLANELINES`
  líneas (`16*planes = 64` para 4 planos) en la planelínea
  `y = (mapposx & 15) * BLOCKPLANELINES`. El `x` es plane-shifted a la derecha
  (`BITMAPWIDTH + (videoposx & ~15)`) y el wrap vertical es implícito por el
  fetch lineal de 42 bytes (`DDFSTRT=$30`).
- La guarda de 1 word (`saveword`/`savewordpointer`) evita el artefacto de
  2 bytes al invertir la dirección, exactamente como en `xlimited.c`.

La demo es deliberadamente **single playfield de 4 planos** (16 colores) para
ver el X-Limited puro. No hay `surface_origin` ni bandas: el posicionamiento
físico es `frontbuffer + y*BITMAPBYTESPERROW + x` con `y` en planelíneas.

## Controles / movimiento

La cámara avanza 2 píxeles por frame hacia la derecha de forma indefinida
(parámetro `K_SCROLL_SPEED`). Cuando alcanza el final del mapa lógico
(`map_width*16 - SCREEN_W - 16`) hace wrap a 0 y refilla la pantalla; el
contenido del mapa es circular (`TileLayerMap` con `wrap_x/y`), de modo que el
scroll se percibe infinito. El cambio de dirección con joystick (si se pulsa
izquierda) también está soportado y demuestra la restauración de la word de
guarda.

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
  lineales, fetch contiguo, columna entrante en `y=mapy*BLOCKPLANELINES`,
  altura extra `256 + (map_width/blocksPerRow/planes)+1+3` y línea de guarda.
- **Análisis visual**: `bash ./tools/analyze/analyze-demo.sh demos/107_xlimited_corkscrew`
  o `analyze-sequence.sh` si existe.
- **Secuencia XLimited** (`demos/107_xlimited_corkscrew/analyze-sequence.sh`):
  valida 1) scroll continuo a la derecha 100 frames, 2) inversión brusca a la
  izquierda 1 frame sin hueco de 2 bytes (`saveword`/`savewordpointer` — compara
  screenshots y, si el canal lateral 2346 está vivo, lee memoria vía periférico
  `0xB70000`), 3) columna entrante en `y=mapy*BLOCKPLANELINES` plane-shifted.

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

## Arquitectura

- `engine/include/eng/field/xlimited.hpp` — `XlimitedField` + `XlimitedDisplayComposer`.
  Documenta por qué interleaved es obligatorio y por qué el wrap horizontal no
  necesita Copper segmentado. Allocation `BMF_INTERLEAVED-like`:
  `row_bytes*planes*height`, addressing `frontbuffer + y*BITMAPBYTESPERROW + x`.
- `demos/107_xlimited_corkscrew/src/main.cpp` — usa `MinimalBackend`,
  `g_eng_run_status` y `demo::build_tile_cache` (compartido con 106) pero no
  reutiliza la lógica circular (`surface_origin`, recentrado, bandas).

Referencias: `ScrollingTricks/Docs/xlimited-uk.html`,
`amiga-stuff/scrolling_tricks/xlimited.c:68,201,448`,
`docs/architecture/AMIGA_8WAY_SCROLLING.md`.
