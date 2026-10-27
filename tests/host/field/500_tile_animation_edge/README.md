# HOST-500: borde del anillo de `TileAnimator::draw_group`

## Qué cubre

Valida en host la geometría que usa el parche de animaciones de la demo 207, sin WinUAE ni capturas:

- Un tile en `x=336` ocupa la última palabra de una fila de 352 px (`row_bytes=44`) y no se sale de ella.
- Un tile que empieza en `ring_y=280` con `display_height=288` se parte en dos segmentos de 8 filas; el segundo empieza en la fila 0 del anillo.
- Cada segmento emite seis jobs (un plano por job) con el módulo y el stride del destino interleaved (`row_bytes * plane_count`).
- El segundo segmento conserva el offset de fuente correcto (fila 8 del frame) y comparte la máscara.

## Contrato que fija

| Elemento | Valor esperado |
|---|---|
| Ancho de fila del destino | `44` bytes = `352` px |
| Planos | `6` |
| Stride de fila interleaved | `44 * 6 = 264` bytes |
| Módulo de fila | `mod16(264 - 2)` |
| Jobs por tile sin cruce | `6` |
| Jobs por tile con cruce vertical | `12` (2 segmentos) |
| Minterm | `$CA` (cookie-cut) |

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/field/500_tile_animation_edge
```

Salida esperada: `HOST-500: borde de anillo OK`.

## Alcance

El test comprueba **geometría de jobs** (destino, segmentación, fuente, máscara y minterm). No ejecuta el Blitter ni valida los píxeles finales; eso corresponde a la sonda aislada de la demo 207 con GDB/checksum.
