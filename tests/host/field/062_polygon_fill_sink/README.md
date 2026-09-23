# HOST-062 — seam de relleno de polígonos (`PolygonFillSink`)

Test host del **seam de relleno por hardware** del engine
(`eng::field::PolygonFillSink` + `Playfield::fill_polygon`, en
`engine/include/eng/field/playfield.hpp`).

## Qué fija

- Con un sink instalado, `Playfield::fill_polygon` **delega** y el relleno CPU no
  toca el bitmap.
- La geometría que recibe el sink es la del layout del playfield:
  `plane_base` (inicio del plano 0), `planes`, `plane_stride`, `row_stride`,
  `row_bytes` y `bitmap_w/h`.
- El default es **interleaved** (`plane_stride = row_bytes`,
  `row_stride = planes*row_bytes`); un playfield con planos **contiguos** lo
  sobrescribe (`plane_stride = row_bytes*height`, `row_stride = row_bytes`).
- Las coordenadas llegan **recortadas** al clip de la `Surface`.
- Sin sink (o con un sink vacío) cae al **relleno CPU** por scanline.

## Cómo corre

```bash
bash tools/run-host-tests.sh tests/host/field/062_polygon_fill_sink
```

## Relación

- Consumidor hardware: `eng::amiga::PolygonFillService`
  (`engine/include/eng/platform/amiga/polygon_fill.hpp`), que enruta al
  `AmigaBackend::blitter_fill_polygon_strided` (máscara 1 bit + cookie-cut).
- Verificado en hardware por la demo `demos/amiga/110_ylimited_shooter` (cara FG
  del DPF rellenada por Blitter a través de `mesh_render_filled`).
