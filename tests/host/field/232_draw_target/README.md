# HOST-232: `DrawTarget` (Surface + Rasterizer + FramePlan + clip)

Test host de `eng/field/draw_target.hpp`: el **objetivo de dibujo** que agrupa `Surface`,
`Rasterizer`, `FramePlan` y el clip para que el llamador no junte los tres a mano.

## Qué comprueba

Se enlaza un `ContiguousPlayfield` a memoria host y se dibuja con `DrawTarget`:

1. `box()` devuelve el clip del destino.
2. `fill(Box, color)` deja 8 bits en el plano 0 y no toca los demás.
3. `fill_run(Box, color)` (tramo horizontal por CPU, el camino del materializador de capas indexadas) pinta el tramo exacto y se recorta al clip.
4. `line(...)` deja una fila completa (32 bits) en el plano 1.
5. `frame(Box, color)` deja el perímetro (2 filas + 2 columnas) en el plano 2.
6. `c2p(req)` (vía `Rasterizer::c2p`) escribe 16 bits en el plano 3.
7. `bitmap(asset, destino, origen, shift)` construye **un** `CopyRect` con ventana: offset de origen, shift fino, módulo derivado del **stride real** del asset (una ventana más estrecha que la fila salta el resto) y rechazo cuando la guarda del shift no cabe.

Las comprobaciones cuentan **bits por plano** (no bytes exactos): el playfield escribe
palabras y el orden de bits depende del endianness del host; el conteo es portable.

Motivo: antes, dibujar exigía coordinar `Scene::surface()`, el `Rasterizer` del playfield y el
`FramePlan`, y `Playfield` tenía que conocer el seam de C2P (lo que forzaba un método de
`Playfield` definido en `raster.hpp`). Con `DrawTarget`, el C2P se resuelve contra el
`Rasterizer` del playfield y `Playfield` deja de conocer el seam.

## Salida de referencia

```
OK: DrawTarget (fill/fill_run/line/frame/c2p/circle/bitmap + box) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/field/232_draw_target
```
