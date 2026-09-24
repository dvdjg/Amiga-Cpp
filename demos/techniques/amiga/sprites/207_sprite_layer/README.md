# 207 — capa de fondo con `effects::SpriteLayer`

Demuestra la capa de fondo hecha con **canales de sprite rearmados horizontalmente** (sprite-as-playfield): `effects::SpriteLayer` cubre una banda con 8 columnas de 16 px. Los 4 primeros canales son **columnas DMA** (columna alta, toda la banda, con su DATA en Chip RAM); los 4 últimos son **columnas Copper** (el Copper reescribe posición y DATA en cada línea). La capa va **detrás** del playfield (`BPLCON2`).

## Estado: validado

La capa cubre **8 columnas de 16 px** (128 px) con patrón rayado y colores por par, sobre el playfield (fondo). Un `WAIT` por línea y luego una **ráfaga** de `SPRxCTL`+`SPRxPOS`+`SPRxDATB`+`SPRxDATA` de los canales Copper; el `WAIT` cae en `arm_hpos` (por defecto 0x40), **después del fetch DMA** de sprites y **antes** de la primera columna, para que la DATA del Copper gane a la del DMA.

Los canales Copper **también** necesitan una estructura DMA válida (`[POS, CTL, DAT0, DATB0, …, 0, 0]`): aunque el Copper los alimente, el DMA del canal sigue leyendo de `SPRxPT`; sin cabecera/terminador válidos avanza por memoria, lee una cabecera basura y deja una **columna fantasma** armada fuera de la banda (`docs/reference/emulators/winuae/sprite-dma.md`).

```
   bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/207_sprite_layer --debug
   bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/207_sprite_layer --warp
```

## Parámetros

- Banda: líneas 60..99 (40 líneas), 8 canales, `hpos0 = 144`, `hpos_step = 16` (columnas contiguas).
- Canales 0..3 DMA (patrón ajedrezado), canales 4..7 Copper (pattern `DATA = 0xAAAA`, barras verticales).
- Una estructura DMA por canal (`kDmaStride = 2 + 40*2 + 2` words): cabecera POS+CTL, 40 líneas de DATA y terminador.
- `BPLCON2 = 0x0008` (sprites detrás de PF1 = fondo). Scroll: `hpos` avanza 2 px por frame.

## Referencias

- Fuente original: `spr_layer/Sprite_Layer/` (Jeroen Knoester, 2018) — `GFX/layer.asm`,
  `Data/copperlists.asm`.
- `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`
- `docs/reference/emulators/winuae/sprite-dma.md`
- `effects::SpriteLayer` (`engine/include/eng/api/effects.hpp`)
