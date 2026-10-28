# HOST-500: borde del anillo de `TileAnimator::draw_group`

## Qué cubre

Valida en host la geometría que usa el parche de animaciones de la demo 207, sin WinUAE ni capturas:

- Un tile de 16 px en `x=336` ocupa la última palabra de una fila de 352 px (`row_bytes=44`) y no se sale de ella.
- Un tile que empieza en `ring_y=280` con `display_height=288` se parte en dos segmentos de 8 filas; el segundo empieza en la fila 0 del anillo.
- Un segmento emite UN job para todos los planos con el módulo y el stride del destino interleaved (`row_bytes * plane_count`).
- El segundo segmento conserva el offset de fuente correcto (fila 8 del frame) y comparte la máscara.
- Con **3 planos**, `draw_group` emite UN job (`bitplane_count=3`) y el módulo usa `row_bytes * 3`, sin constante de 6.
- Con **tile de 32×32**, cada fila usa 2 palabras, el offset de plano de fuente es 128 bytes y el frame mide 768 bytes.

## Contrato que fija

| Elemento | Valor esperado |
|---|---|
| Ancho de fila del destino | `44` bytes = `352` px |
| Planos | `6` |
| Stride de fila interleaved | `44 * 6 = 264` bytes |
| Módulo de fila | `mod16(264 - 2)` |
| Jobs por tile sin cruce | `1` (`bitplane_count = planes`; 6 en 16 px, 3 en el caso de 3 planos) |
| Jobs por tile con cruce vertical | `2` (uno por segmento, cada uno con todos los planos) |
| Tile de 32 px | `words_per_row=2`, stride de plano de fuente `128` bytes |
| Minterm | `$CA` (cookie-cut) |

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/field/500_tile_animation_edge
```

Salida esperada: `HOST-500: borde de anillo OK`.

## Alcance

El test comprueba **geometría de jobs** (destino, segmentación, fuente, máscara y minterm). No ejecuta el Blitter ni valida los píxeles finales; eso corresponde a la sonda aislada de la demo 207 con GDB/checksum.
