# 111 — Side-scroller horizontal X-Limited (DPF con objetos por Blitter)

Demo de scroll **X-Limited** (anillo largo en X, Y corto/fijo) con **dual playfield
heterogéneo**: PF1 = tilemap del corkscrew, PF2 = **lienzo plano de objetos** (nave,
balas) delante.

Mundo **4096 × 320 px** (256 × 20 celdas de 16×16) servido por un
`StreamingWorldMap` (chunks residentes en un pool del llamador); el playfield solo
ve una `TileMapView`. Perfil de scroll `ScrollProgressive` (2 px/frame) por defecto;
cambiando `ScrollProfile_t` a `ScrollFast1/2/4` el scroll avanza a saltos de tile.

## Relleno de polígonos por Blitter (validación del subsistema)

Además de la nave/balas, la demo dibuja un **cubo sólido** de la capa FG con el
pipeline 3D del engine (`mesh_render_filled` → `Surface::fill_polygon`). El relleno
de cada cara lo hace el **Blitter** por el seam `eng::field::PolygonFillSink`, que
enruta a `MinimalBackend::blitter_fill_polygon_strided` (máscara 1 bit en Chip +
contorno `ONEDOT` + area fill inclusivo + cookie-cut a cada plano) con la geometría
**interleaved** del lienzo FG (`plane_stride = row_bytes`,
`row_stride = planes*row_bytes`). La demo no ve registros ni planos.

El cubo se dibuja **una vez en `init`** (estático): el lienzo FG es de un solo
buffer, y animarlo por frame (borrar + redibujar sobre lo que se muestra) produce
flicker/tearing. `g_eng_run_status.detail` bits 27..24 reportan las caras
dibujadas.

- Motor y seam: `engine/include/eng/field/playfield.hpp` (`PolygonFillSink`),
  `engine/src/platform/amiga_minimal/amiga_minimal.cpp`
  (`blitter_fill_polygon_strided`), `engine/include/eng/platform/amiga/polygon_fill.hpp`
  (`PolygonFillService`).
- Test host: **HOST-062** (`tests/host/062_polygon_fill_sink`).

## Manejo

`bash tools/build/build-demo.sh demos/amiga/111_xlimited_sidescroller --debug`
`bash tools/run/run-demo.sh demos/amiga/111_xlimited_sidescroller`
`bash tools/analyze/analyze-demo.sh demos/amiga/111_xlimited_sidescroller`
