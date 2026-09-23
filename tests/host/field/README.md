# Tests HOST — field

Categoría `field` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-023 | [limited_axes](023_limited_axes/README.md) | Ejes del `ScrollEngine`: `Ring`/`Finite`/`Off` y `OneDirection` (shooter vertical). |
| HOST-024 | [parallax_offset](024_parallax_offset/README.md) | Offset de fondo parallax/fijo y compensación del Copper split ("soft DPF"); sin saltos de columna. |
| HOST-025 | [tile_source](025_tile_source/README.md) | Concepto `TileSource`: mapa denso (`TileLayerMap`) y **disperso por chunks** (`SparseTileMap`); skip de `empty_tile`. |
| HOST-026 | [chunk_cache](026_chunk_cache/README.md) | `ChunkCache`: chunks residentes con carga bajo demanda y evicción LRU (mundo disperso sin todo el mapa en RAM). |
| HOST-029 | [streaming_map](029_streaming_map/README.md) | `StreamingWorldMap`: prefetch de la ventana, lectura de solo-residentes, chunk ausente como `empty_tile` y evicciones LRU. |
| HOST-030 | [tile_map_view](030_tile_map_view/README.md) | `TileMapView`: accesor de scroll con límites/wrap sobre un `TileSource` (streaming/disperso); `wrap_period`. |
| HOST-031 | [world_view](031_world_view/README.md) | `WorldView`: chunk `WorldMap` (cabecera, directorio ordenado, celdas, wrap, chunks ausentes, metadatos y validación de bloques). |
| HOST-032 | [scroll_profile](032_scroll_profile/README.md) | `ScrollProfile`: selección estática del scroll (paso por frame, guarda de lookahead, prefill, invariante) con alias `ScrollProgressive`/`ScrollFastN`. |
| HOST-033 | [scroll_burst](033_scroll_burst/README.md) | `snap_to_tiles` y `y_staging_tiles`: avance por tiles completos (dirección laceda) y staging vertical del corkscrew por perfil. |
| HOST-034 | [scroll_burst_equiv](034_scroll_burst_equiv/README.md) | `burst_right` equivalente bit a bit a los sub-pasos de 1 px (dibujos, `save_word`, estado) en varios escenarios. |
| HOST-035 | [world_layer](035_world_layer/README.md) | `WorldLayerSource` (capa de `WorldView` como `TileMap`) y `WorldMapChunkLoader` (Loader-RAM): WorldMap → `StreamingWorldMap` → `TileMapView`. |
| HOST-036 | [scroll_target_emitter](036_scroll_target_emitter/README.md) | Contrato del scroll dividido: `ScrollTarget` (layout) + `ScrollEmitter` (dibujo/costura) = `ScrollSink`; `XLimitedPlayfield` cumple las tres. |
| HOST-037 | [world_uaf](037_world_uaf/README.md) | Cadena UAF-R → `WorldView`: ensamblar blob con chunk `WorldMap` (`BlobWriter`), validar con `Blob` y leer el mundo. |
| HOST-038 | [plane_view](038_plane_view/README.md) | `PlaneView`: soft DPF (front/back/`flip`) con doble buffer opcional de planos; base del `SoftDpfComposition`. |
| HOST-039 | [soft_dpf](039_soft_dpf/README.md) | `SoftDpfComposition`: composición soft DPF (doble buffer + blit de copia del patrón) extraída del playfield. |
| HOST-043 | [xlimited_tile_bank](043_xlimited_tile_bank/README.md) | `XlimitedTileBank`: banco de bloques propio o aliaseado (vista + `MemoryKind`); `Block<Tag>` propaga el `kind` de la reserva. |
| HOST-044 | [big_buffer_scroll](044_big_buffer_scroll/README.md) | `BigBufferScroll`: estrategia de scroll trivial (offset de cámara acotado / anillo); contraste con `ScrollEngine`. |
| HOST-045 | [surface_polygon](045_surface_polygon/README.md) | `Surface::fill_polygon`: rasterizado CPU de polígono convexo por scanline (interior/exterior, triángulos, clip). |
| HOST-061 | [flat_mapper](061_flat_mapper/README.md) | `eng::field::map_flat_scroll`: mapper neutral cámara→`planeaddx`/`planeaddy`/`BPLCON1`/`BPLMOD` del virtual playfield (fetch ancho `$30`). |
| HOST-062 | [polygon_fill_sink](062_polygon_fill_sink/README.md) | `eng::field::PolygonFillSink`: seam de relleno por hardware (`Playfield::fill_polygon`), con geometría planar correcta (interleaved/contiguo) y fallback CPU. |
| HOST-063 | [ring_mapper](063_ring_mapper/README.md) | `eng::field::map_ring_scroll`: mapper neutral del corkscrew (planeaddx/BPLCON1 con fetch ancho, offset del anillo y split); fórmula extraída de `XLimitedPlayfield` con equivalente compile-time/runtime. |
| HOST-068 | [double_buffer_scroll](068_double_buffer_scroll/README.md) | `eng::field::DoubleBufferScrollPlayfield`: 2 bitmaps, `flip()` y `hardware_view()` del delantero. |
| HOST-106 | [scroll_saveword_guard](106_scroll_saveword_guard/README.md) | `field/scroll_engine.hpp`: la costura (`save_word`) se **restaura** cuando `add_draw` rechaza el frame (ScopeGuard); en el camino correcto no se restaura. |
| HOST-232 | [draw_target](232_draw_target/README.md) | `field::DrawTarget` (Surface+Rasterizer+FramePlan+clip): `fill`/`line`/`frame`/`c2p` y `box()`. |
| HOST-266 | [rect_fill_sink](266_rect_fill_sink/README.md) | Relleno de rect por hardware: `RectFillSink` + `fill_rect_hw` + elección del `BlitterRaster`. |
