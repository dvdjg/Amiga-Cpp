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
| Bitmap de scroll | ancho mapa+solape × **alto 448** = **158 KB** | anillo **352×320** a 3 planos = **~41 KB** |
| Eje Y | puntero (bitmap alto) | corcóscru (anillo + split), 8 direcciones |

El corcóscru solo reserva `(viewport_w + guarda_fetch) × (viewport_h + 2·tile)` planos: para 320×208
a 3 planos son **~31 KB** (≈5× menos que la 204). El banco X-Limited del atlas se **alía**
(`blocks_prebuilt`), no se copia.

## Qué demuestra

- **`XlimitedScene`** con `blocks_prebuilt` (el banco X-Limited del atlas) y `map` = el mapa del
  atlas (40×40, toroidal). **Sin** `fg_row_fn` procedural.
- **Viewport 320×208** (split de Copper canónico: `0x2c + 208 = 252 ≤ 255`), anillo vertical
  `display_height = 320` (ventana 208 + recorrido Y 112).
- **Ruta fina** (tabla de seno 256 muestras que avanza cada frame) → **cada frame es una imagen
  distinta** (verificado: 0 frames idénticos en 80 consecutivos).
- Mismo `Eng` theme que la 204 (no usa `World`: `TileLayerMap` directo).

## Anillo y Y (toro)

El anillo vertical es `display_height = 320` (20 tiles = ventana 208 + recorrido Y `kYMax = 112`).
El scroll Y es el **toro** del anillo: al envolver, el Copper re-apunta los `BPLxPT` a la fila 0
(`split_planeaddy = 0`). Con 288 la banda inferior salía equivocada (el anillo no llegaba a cubrir
la envoltura); con 320 renderiza el pueblito **coherente** a lo largo de todo el recorrido.

> El direccionamiento del tileset **no** era el problema: el bloque (`block/20`, `block%20`) y el
> banco X-Limited del atlas coinciden pixel a pixel con la 204.

## Validación

- Renderiza el pueblito (capturas revisadas a lo largo del recorrido Y), coherente con la 204.
- Secuencia consecutiva: scroll fino, **0 frames repetidos** en 80 seguidos (204 y 205).
