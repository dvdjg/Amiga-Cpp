# 214 - app_sprite: informe de validación

Informe de validación de la demo (procedimiento F0–F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`). Pasada de perfilado, ablaciones y corrección 2026-10.

## Estado

**ABIERTA (cadencia) — mejora 44×**. La imagen es correcta (fondo planar con scroll suave de 1 px, sprite por `screen.sprite` y actor por `draw_world`; visión canónica sin artefactos) y la cadencia sube de **0,70 fps / 71,5 campos** a **30,96 fps / 1,61 campos / 229.108 ciclos** en `A500_debug`. Queda fuera de 1 VBlank: la copia completa del fondo por Blitter cuesta ~0,5 campo y el frame reparte entre 1 y 2 campos (cadencia mixta). El camino a 1 campo es la **capa de scroll por hardware** (DT-030).

## Medidas (`A500_debug`, `tools/debug/measure-fps.mjs`, 20 s)

| Medida | Antes | Ahora |
|---|---|---|
| fps emulado | 0,70 | 30,96 |
| campos/frame | 71,54 | 1,61 |
| ciclos/frame | 10.150.143 | 229.108 |

## Causa raíz (ablaciones)

El coste estaba en `materialize_bitmap_layers` (`world_render.hpp`), que re-materializaba el fondo indexado **por CPU cada frame**. Ablaciones (todas medidas):

| Variante | Resultado |
|---|---|
| `emit_row` sin pintar (solo recorrer filas) | 49,92 fps / 1,00 campos |
| Escanear los runs **sin** pintar | 1,40 fps / 35,6 campos / 5,05 M |
| 1 run por fila (toda la fila) | 1,85 fps / 27,1 campos / 3,84 M |
| Escribir 20.480 words a un bloque Chip (bucle ajustado) | 7,73 fps / 6,46 campos (≈38 ciclos por acceso) |
| Release | igual de lento (1,89 fps) |

Conclusión: el materializador hace ~164 k lecturas + ~25 k escrituras a Chip por frame y cada acceso cuesta ~30–40 ciclos (contención con el DMA de bitplanes): **cualquier re-materialización completa por CPU es infactible** (mínimo ~0,8 M de ciclos solo en escrituras). El patrón del fondo no es la variable: incluso con runs largos no cabe. Por eso la solución no es optimizar el escritor CPU (se probó `fill_run`/`draw_run_words`, sin efecto) sino **no re-materializar**: pre-render planar + copia por Blitter.

## Corrección aplicada

1. **Pre-render planar una vez** (`eng::graphics::c2p_1x1_naive`, 640×256×4, ~0,7 s en `init`).
2. **Copia de la ventana visible por Blitter** cada frame con `screen.bitmap(asset, destino, palabra_origen, shift)`: **un solo job** de 4 planos, palabra de origen `scroll & ~15` y **una palabra extra** para el barrel shift (sin ella aparece una línea negra de 16 px en el borde izquierdo; verificado A/B). El motor deriva el **módulo del stride real** del asset: el `0` fijo de la rama contigua solo valía cuando `row_bytes == ancho copiado` — era el bug latente de `Screen::bitmap`, arreglado y cubierto por HOST-232.
3. Scroll de 2 px/frame con vuelta al agotar la ventana extra del asset (640 − 320 px).
4. El mundo retenido se conserva para el **actor** (segunda vía de representación); el fondo deja de ser capa `Bitmap` del `World` (su materializador CPU no cabe, ver arriba).

## Hallazgos colaterales (registrados en DT-030)

- **`Screen::bitmap` no pintaba** (bug latente sin consumidores): la rama contigua fijaba los módulos de origen y destino a 0, válido solo si la fila del asset coincide con el ancho copiado. **Arreglado** (módulos desde los strides) + ventana con origen/shift, y con test de construcción en HOST-232; la 214 es su primer consumidor.
- **Escena de 2 planos patológica**: con `color_depth = 2` la base sin copia mide 853 k ciclos/frame (6 campos) frente a 154 k (1,09 campos) con 4 planos; el modo de 2 planos del compositor merece investigación aparte.

## Alcance y límites

Cubierto: fps/ciclos, ablaciones, imagen en secuencia con visión canónica (`src/VISION.md`), scroll suave de 1 px sin artefactos. **SIN VERIFICAR**: cadencia estable a 1 campo (requiere la capa de scroll por hardware) y release.
