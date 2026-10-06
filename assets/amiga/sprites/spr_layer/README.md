# Assets — `spr_layer` (capa de sprites free form)

Assets fuente de la demo [`213_spr_layer`](../../../../demos/techniques/amiga/sprites/213_spr_layer/README.md),
tomados de la referencia **«SPR Layer»** de Jeroen Knoester (2018, `spr_layer/Sprite_Layer`).

## Ficheros

| Fichero | Qué es |
|---|---|
| `bg_tiles_spr.raw` | Tileset de **fondo** para la capa de sprites: 13 tiles de 16x16x2 planos (64 B/tile). Lo usa la demo por `INCBIN`. |
| `bg_tiles_spr.iff` | Imagen fuente original del tileset de fondo (IFF ILBM). |
| `fg_tiles.raw` | Tileset de **primer plano** (63 tiles de 32x32x4). No usado aún por la demo (playfield omitido). |
| `fg_tiles.iff` | Imagen fuente original del primer plano. |
| `sb_tiles.raw` | Tiles del **subbuffer** (4 tiles de 16x16x3). No usado aún. |

El **tilemap** de fondo (32x14 tiles) vive en el código de la demo (`kTileMap`), extraído de
`GFX/tilemap.asm` de la referencia.

## Licencia y atribución

- **Tiles**: **Turrican II** (no creados por el autor de la referencia; ver `ReadMe.txt`).
- **Código/algoritmo de referencia**: (C) 2018 Jeroen Knoester, con permiso de reutilización
  citando autoría. Startup de la referencia: Photon (Scoopex).

Estos ficheros se incluyen como **assets fuente** de estudio de la técnica; no se regeneran por
pipeline (la demo los incrusta con `INCBIN`).
