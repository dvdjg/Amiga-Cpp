# 207 — capa de fondo con `effects::SpriteLayer`

Demuestra la capa de fondo hecha con **canales de sprite rearmados horizontalmente**
(sprite-as-playfield): `effects::SpriteLayer` emite, por línea y canal, un
`SpriteHorizontalRearm` (misma línea, otra X) con **scroll** por frame. Es una capa de
**fondo** (los sprites van detrás del playfield vía `BPLCON2`).

## Estado: validado

La capa cubre **8 columnas de 16 px** (128 px) con patrón rayado y colores por par, sobre el
playfield (fondo). El patrón correcto (de Jeroen Knoester, `spr-layer.html`) es: **un `WAIT`
al inicio de cada línea** y luego una **ráfaga** de `SPRxPOS`+`SPRxDATB`+`SPRxDATA` de todos
los canales (el `SPRxCTL` se fija una vez por banda). Un `WAIT` por canal en su X (lo que
hacía la primera versión) **no** funciona: solo algunos canales llegaban a armarse.

```
   bash ./tools/build/build-demo.sh demos/amiga/207_sprite_layer --debug
   bash ./tools/run/run-demo.sh demos/amiga/207_sprite_layer --warp
```

## Parámetros

- Banda: líneas 60..99 (40 líneas), 8 canales, `hpos0 = 32`, `hpos_step = 16` (columnas contiguas).
- Patrón: `data_high = 0xAAAA` (color 1 del par), `data_low = 0`.
- `BPLCON2 = 0x0008` (sprites detrás de PF1 = fondo).

## Referencias

- Fuente original: `spr_layer/Sprite_Layer/` (Jeroen Knoester, 2018) — `GFX/layer.asm`,
  `Data/copperlists.asm`.
- `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`
- `effects::SpriteLayer` (`engine/include/eng/api/effects.hpp`)
