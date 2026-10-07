# Assets — `spr_layer` (capa de sprites free form)

Assets fuente de las demos [`213_spr_layer`](../../../../demos/techniques/amiga/sprites/213_spr_layer/README.md)
y [`218_free_form_sprite_layer`](../../../../demos/techniques/amiga/sprites/218_free_form_sprite_layer/README.md),
tomados de la referencia **«SPR Layer»** de Jeroen Knoester (2018, `spr_layer/Sprite_Layer`).

## Ficheros

| Fichero | Qué es |
|---|---|
| `bg_tiles_spr.raw` | Tileset de **fondo** para la capa de sprites: 13 tiles de 16x16x2 planos (64 B/tile), con los planos **intercalados por línea** (`[l0 p0, l0 p1, l1 p0, …]`), que es el orden que consumen los blits de la referencia. |
| `bg_tiles_spr.iff` | Imagen fuente original del tileset de fondo (IFF ILBM). |
| `fg_tiles.raw` | Tileset de **primer plano** (63 tiles de 32x32x4, 512 B/tile), planos intercalados por línea. Lo usa la demo 218. |
| `fg_tiles.iff` | Imagen fuente original del primer plano. |
| `sb_tiles.raw` | Tiles del **subbuffer** (4 tiles de 16x16x3, 96 B/tile), planos intercalados por línea. Lo usa la demo 218. |
| `bob_4bpl.raw` | BOB de 32x32x4 (512 B) **planar** (4 planos contiguos de 128 B). Lo usa la demo 218 (cookie-cut). |
| `mask_4bpl.raw` | Máscara del BOB (32x32x4, planar; 0 donde se ve el BOB). La usa la demo 218. |
| `font8x8.raw` | Fuente de 8x8 (95 glifos ASCII 32..126, 8 B/glifo) de `GFX/font.asm`. Solo la lee la CPU. |

Los **tilemaps** (fondo 32x14, foreground 32x7 y subbuffer 18x1) viven en el código de la demo
218, extraídos de `GFX/tilemap.asm` de la referencia.

## Licencia y atribución

- **Tiles**: **Turrican II** (no creados por el autor de la referencia; ver `ReadMe.txt`).
- **Código/algoritmo de referencia**: (C) 2018 Jeroen Knoester, con permiso de reutilización
  citando autoría. Startup de la referencia: Photon (Scoopex).

Estos ficheros se incluyen como **assets fuente** de estudio de la técnica; no se regeneran por
pipeline (las demos los incrustan con `INCBIN`).
