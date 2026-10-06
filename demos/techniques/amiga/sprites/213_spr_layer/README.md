# 213_spr_layer — capa de sprites *free form* a pantalla completa con scroll

Recreación en el engine de la demo de referencia **«SPR Layer»** de Jeroen Knoester (2018,
`../spr_layer/Sprite_Layer`): un **fondo de sprites** que ocupa **toda la pantalla** con una imagen
**no repetitiva** (cada columna de 16 px es distinta) y hace un **scroll horizontal suave** de
1 px/frame. Usa el tileset de fondo de **Turrican II** de la propia referencia (ver
`assets/amiga/sprites/spr_layer/README.md`).

El playfield de 4 planos de la referencia se omite: la **capa de sprites es el contenido** a
pantalla completa (1 plano transparente delante, sprites detrás). No se usa
`effects::FreeFormSpriteLayer` (roto; ver demo 212): el algoritmo se implementa **directamente**
sobre el `Scheduler` de Copper.

## La técnica (por qué funciona)

Los 8 canales de sprite del Amiga cubren, sin trucos, solo 128 px. Para llenar 320 px el Copper
**rearma cada canal dentro de la misma línea**: reescribe `SPRxPOS`+`SPRxDATB`+`SPRxDATA` para
redibujarlo más a la derecha con **otra imagen**, y al final de la línea lo **reposiciona a la
izquierda** (orden inverso) para el renglón siguiente. Claves verificadas
(`docs/reference/amiga/techniques/sprite-horizontal-multiplex.md` §46-70):

1. **Por posición SOLO `SPRxPOS`+`SPRxDATB`+`SPRxDATA`** — **no** `SPRxCTL` (escribirlo desactiva
   el comparador; `SPRxDATA` arma el sprite en la nueva X).
2. **Reposición de fin de línea en orden inverso** (7..0), antes de que el haz alcance la X.
3. Cada canal (DMA y Copper) necesita una **estructura DMA válida** (cabecera `POS`+`CTL` +
   terminador) o el DMA avanza por memoria y deja una **columna fantasma**.

Geometría: 22 columnas de 16 px (8 por **DMA** = las 8 primeras + 14 por **Copper**), `WAIT` al
inicio de cada línea (`arm_hpos=0x30`, tras el fetch DMA), 224 líneas desde `first_line=16`.

## El scroll (suave, ~0 CPU en el fino)

- **Fino (0..15 px):** la X de **todas** las columnas se desplaza 1 px/frame. Se parchea la palabra
  de **valor** de cada `SPRxPOS` con `blitter_fill_words_strided` (14 fills estridados, uno por
  columna Copper) + los 8 `POS` de cabecera DMA (CPU, trivial). **No** se re-emite la copperlist.
- **Grueso (cada 16 px):** entra una columna nueva del mundo; se reescriben los `DATB`/`DATA` de
  las 14 columnas Copper en la copperlist y se regeneran las estructuras DMA (`patch_data`).
- **Ping-pong** sobre el mundo (32 columnas = 512 px) para no salirse.

## Contrato que ilustra

```cpp
// 1) Mundo (32x224) desde el tilemap + tileset: (DATB, DATA) por línea.
// 2) 8 estructuras DMA: [POS, CTL, (DATA,DATB)*224, 0,0] — cabecera parcheable.
// 3) Copperlist: display + punteros de sprite + por línea:
//      WAIT(arm_hpos)
//      14x [SPRxPOS, SPRxDATB, SPRxDATA]   (reusa canales j%8)
//      8x  [SPRxPOS]                        (reposición a la izquierda, orden inverso)
// 4) Por frame: patch_pos(s) (fino, Blitter); al cruzar columna, patch_data(window).
```

## Estado

- **Funciona**: el fondo llena la pantalla y el scroll es continuo y suave. Validado con visión
  (Ollama, secuencia de 6 frames): «movimiento horizontal claro y continuo… sin artefactos ni
  costuras». Plan/coste no aplican (no hay búsqueda).
- **Mejoras posibles**: doble copperlist (como la referencia, 4) para re-emitir en VBlank sin
  carrera; repartir la actualización de DATA por Blitter en varios frames (la referencia usa 32);
  incluir el playfield de 4 planos y los BOBs de la referencia.

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/213_spr_layer --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/213_spr_layer --keep-running
# secuencia para validar el movimiento:
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/213_spr_layer --sequence-frames 6 --sequence-interval-ms 250
```

## Referencia

- `../spr_layer/Sprite_Layer` — «SPR Layer», Jeroen Knoester (2018). Código 68000 + assets.
- `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md` — la técnica (free form).
- `docs/reference/amiga/techniques/sprite-layer.md` — subsistema de capas de sprite del engine.
