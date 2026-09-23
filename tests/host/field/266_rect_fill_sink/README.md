# HOST-266: relleno de rectángulo por hardware (`RectFillSink`)

Test host del *seam* de **relleno de rectángulo axis-aligned por hardware**
(`eng::field::RectFillSink` + `Playfield::fill_rect_hw` + `BlitterRaster::fill_rect`).

## Qué comprueba

1. **Delegación**: con sink instalado, `fill_rect_hw` llama al sink con la geometría planar
   correcta (planos, `plane_stride`, `row_stride`, `row_bytes`, dimensiones, rect y color) y
   **no** toca el buffer.
2. **Fallback CPU**: sin sink, rellena por CPU (`draw_span` por fila): interior pintado,
   exterior no.
3. **Elección del raster**: `BlitterRaster::fill_rect` usa el sink si `Auto` y área ≥ umbral;
   con área < umbral o modo `Cpu`, cae a CPU (sink no llamado).
4. **Layout contiguo**: el playfield reporta otros strides al sink.

## Notas

- La ruta real por Blitter (`AmigaBackend::blitter_fill_rect`, D-only + reparación de bordes)
  se valida **en hardware** con el self-test de la demo 215. Aquí se fija el **contrato** del
  *seam* que esa ruta consume.
- El Blitter solo accede a **Chip RAM**: por eso el relleno no usa una máscara fuente, sino
  minterm `$FF`/`$00` (D-only) con la primera/última palabra reparadas por CPU.

## Salida de referencia

```
OK: RectFillSink (delegacion del rect + strides + eleccion del raster) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/field/266_rect_fill_sink
```
