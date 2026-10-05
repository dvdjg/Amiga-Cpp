# 212_free_scroll_layer — Free Form Sprite Layer: fondo de 320 px **NO repetitivo** + scroll

**App de alto nivel** (`eng/api/api.hpp`) que monta un **fondo de sprites no repetitivo** de 320 px
(20 columnas de 16) con `eng::effects::SpriteLayer` en modo **free form** (`Config::columns` +
`Config::image`). Los 8 canales dibujan las **8 primeras columnas por DMA** (cada uno con su
estructura en Chip RAM); el **Copper reutiliza** esos canales para el resto reescribiendo
`SPRxPOS`+`SPRxDATB`+`SPRxDATA` (≥24 px entre reusos). La **CPU queda libre** (devora DMA); el
scroll se hace parcheando solo las palabras `SPRxPOS` (`bind`/`patch`, ~0 CPU).

Técnica: [Free Form Sprite Layer](https://powerprograms.nl/amiga/spr-layer.html) (Jeroen Knoester) y
[sprite-horizontal-multiplex.md](../../../../../docs/reference/amiga/techniques/sprite-horizontal-multiplex.md).
Fuente de referencia (las dos claves): `spr_layer/Data/copperlists.asm`.

## Las dos claves del montaje

1. **Por posición, SOLO `SPRxPOS` + `SPRxDATB` + `SPRxDATA`** — **no** se escribe `SPRxCTL`.
   (`SPRxDATA` "arma" el sprite; escribir `SPRxCTL` desactiva el comparador y lo rompe.)
2. **Al final de cada línea** se reposicionan los 8 canales **a la izquierda en orden inverso**
   (`SPRxPOS` solo), para que el renglón siguiente los vuelva a dibujar por DMA en su sitio.

## Uso (ladrillo)

```cpp
eng::effects::SpriteLayer::Config cfg {};
cfg.first_line = 0u; cfg.lines = 255u; cfg.channels = 8u;
cfg.hpos0 = 112u; cfg.hpos_step = 16u;      // margen para el scroll
cfg.columns = 21u;                          // 21 posiciones = 336 px no repetitivos
cfg.image = image;                          // DATA distinta por (columna, línea)
cfg.dma_channels = 8u;                       // las 8 primeras columnas por DMA
cfg.dma_data = structures; cfg.dma_stride = stride;
cfg.bplcon2 = 0x0008u;                       // fondo detrás del playfield
layer.attach(cfg);
layer.bind(sched);                           // emite + registra las palabras SPRxPOS
// por frame: layer.set_scroll(s); layer.patch(words);   // ~0 CPU
```

## Estado

- **Renderiza 320 px no repetitivos** a ancho completo (validado por visión) + **scroll suave** por
  parche de `SPRxPOS` (~0 CPU); 49.92 fps (1 campo/frame). El CPU queda libre; el coste es de DMA de
  Copper (≈ 3 `MOVE` por columna + 8 de reposición de fin de línea, por línea).
- **Pendiente**: una **junta de una columna** (salto de fase) a pulir; y la corrección de la DATA
  (trozos verticales) para que la figura sea exacta.

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/212_free_scroll_layer --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/212_free_scroll_layer --keep-running
```
