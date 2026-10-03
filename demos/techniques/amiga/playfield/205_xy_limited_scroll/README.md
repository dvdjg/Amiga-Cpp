# 205 — el mismo pueblito por **XYLimited** (corcóscru), viewport 320×208 y mínimo framebuffer

Contraparte de la **204** (camino de tiras): **mismo atlas** (“Beginning Fields”), **misma ruta**
(horizontal → vertical → diagonal → circular → Lissajous), pero por el motor **X-Limited** (8
direcciones de verdad), con **viewport 320×208** y el **mínimo framebuffer**.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/205_xy_limited_scroll --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/205_xy_limited_scroll
```

## Framebuffer: mínimo

| | 204 (tiras) | 205 (XYLimited) |
|---|---|---|
| Bitmap de scroll | ancho mapa+solape × **alto 448** = **158 KB** | anillo **352×240** a 3 planos = **~31 KB** |
| Eje Y | puntero (bitmap alto) | corcóscru (anillo + split), 8 direcciones |

El corcóscru solo reserva `(viewport_w + guarda_fetch) × (viewport_h + 2·tile)` planos: para 320×208
a 3 planos son **~31 KB** (≈5× menos que la 204). El banco X-Limited del atlas se **alía**
(`blocks_prebuilt`), no se copia.

## Qué demuestra

- **`XlimitedScene`** con `blocks_prebuilt` (el banco X-Limited del atlas) y `map` = el mapa del
  atlas (40×40, toroidal). **Sin** `fg_row_fn` procedural.
- **Viewport 320×208** (split de Copper canónico: `0x2c + 208 = 252 ≤ 255`), anillo vertical 240.
- **Ruta fina** (tabla de seno 256 muestras que avanza cada frame) → **cada frame es una imagen
  distinta** (verificado: 0 frames idénticos en 80 consecutivos).
- Mismo `Eng` theme que la 204 (no usa `World`: `TileLayerMap` directo).

## Límite de Y (split vertical single-field)

El scroll Y se mantiene en **`[0, 64]`**: el corcóscru single-field solo **reconstruye bien la
ventana mientras NO envuelve el anillo** (`y + tile <= display_height − viewport_h`). Al pasar de
esa cota (split de Copper), la banda inferior muestra filas **equivocadas** del anillo (~2 tiles
rotos) — lo demás coincide pixel a pixel con la 204 (diff < 0.5 en las bandas centrales). **No es
un fallo del tileset**: el direccionamiento del bloque (`block/20`, `block%20`) coincide con el
banco del atlas. Es el split vertical single-field, que queda **pendiente en el engine**
(la 202 lo usa en DPF; el camino single no lo reconstruye bien).

## Validación

- Renderiza el pueblito (captura revisada), coherente con la 204.
- Secuencia consecutiva: scroll fino (1 px/frame), **sin frames repetidos**.
