# HOST-419 — `eng::effects::SpriteLayer` en modo **Free Form**

Test host del **fondo de sprites NO repetitivo** (`engine/include/eng/api/effects.hpp`):
el modo free-form de `SpriteLayer` (`Config::columns` + `Config::image`), que rearma los
canales por Copper a lo ancho reescribiendo posición **y** DATA.

Cubre las **dos claves** del montaje (fuente: `spr_layer/Data/copperlists.asm`, Jeroen Knoester):

- Por posición se emite **solo `SPRxPOS`+`SPRxDATB`+`SPRxDATA`** — **nunca `SPRxCTL`** (escribirlo
  desactivaría el comparador y el rearmado no dibujaría).
- Al final de cada línea se **reposicionan** los canales DMA (solo `SPRxPOS`), en orden inverso,
  para el siguiente renglón.
- Las columnas extra **ciclan** los canales (`k % channels`) y su DATA sale de `image`; el nº de
  MOVEs por línea es `(columns - dma_channels) * 3 + dma_channels`.

Es freestanding (sin hardware): se compila con `g++` del host y corre como binario nativo.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/419_free_form_sprite   # solo este
bash tools/run-host-tests.sh                                           # todos
```
