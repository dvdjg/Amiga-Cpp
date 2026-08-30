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
| `K_TILE_WIDTH` | `16`, `32` | Anchura de tile en píxeles (múltiplo de 16). 32 usa 2 words por fila y `BITMAPBLOCKSPERROW` ajustado. |
| `K_PLANES` | `4` (defecto) | Profundidad. 4 es el caso canónico de X-Limited; otros valores sólo para pruebas de altura extra. |

```bash
# 352 píxeles, tiles 16, 4 planos (modo por defecto)
bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean

# Tiles de 32 píxeles (misma altura, doble anchura)
bash ./tools/build/build-demo.sh demos/107_xlimited_corkscrew --debug --clean -DK_TILE_WIDTH=32
```

## Verificación

- **Test host**: `node tools/analyze/verify-xlimited.mjs` modela direcciones
  lineales, fetch contiguo, columna entrante en `y=mapy*BLOCKPLANELINES`,
  altura extra `256 + (map_width/blocksPerRow/planes)+1+3` y línea de guarda.
- **Análisis visual**: `bash ./tools/analyze/analyze-demo.sh demos/107_xlimited_corkscrew`
  o `analyze-sequence.sh` si existe.

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
