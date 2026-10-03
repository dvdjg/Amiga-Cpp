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
| HOST-071 | [scroll_variant](071_scroll_variant/README.md) | `field/scroll_variant.hpp`: nombres de la referencia ScrollingTricks (XLimited/XUnlimited/YUnlimited(2)/XYLimited/XYUnlimited(2)) → ejes/wrap/fetch de `XlimitedConfigT`; presets tall-Y y wide-X (`_64`, 384 px). |
| HOST-243 | [scroll_coherence](243_scroll_coherence/README.md) | `field/scroll_engine.hpp`: coherencia de la **selección de plaquetas** del corkscrew sobre mapas aleatorios — cada columna cubre el anillo completo (contiguo, sin huecos) a 1 px y a 16 px, misma secuencia; tope en mapa acotado; límite `bpc >= tile_w + 2`. |
| HOST-244 | [strip_geometry](244_strip_geometry/README.md) | Geometría e invariantes del **scroller de tiras** (anillo de Copper): constantes (anillo 23 words, `BPL_MOD=184`, `BLTDMOD=44`), guarda fuera de la ventana, cobertura completa tras cada cruce a pasos 1..16, y el límite OCS del split XY (VPOS 8 bits). |
| HOST-245 | [strip_composer](245_strip_composer/README.md) | `field/strip_composer.hpp`: emite la copperlist una vez y por frame **parchea** `BPLCON1` + `BPLxPT` por plano + split (two-WAIT si cruza la 255), sin re-emitir. |
| HOST-045 | [surface_polygon](045_surface_polygon/README.md) | `Surface::fill_polygon`: rasterizado CPU de polígono convexo por scanline (interior/exterior, triángulos, clip). |
| HOST-061 | [flat_mapper](061_flat_mapper/README.md) | `eng::playfield::map_flat_scroll`: mapper neutral cámara→`planeaddx`/`planeaddy`/`BPLCON1`/`BPLMOD` del virtual playfield (fetch ancho `$30`). |
| HOST-062 | [polygon_fill_sink](062_polygon_fill_sink/README.md) | `eng::playfield::PolygonFillSink`: seam de relleno por hardware (`Playfield::fill_polygon`), con geometría planar correcta (interleaved/contiguo) y fallback CPU. |
| HOST-063 | [ring_mapper](063_ring_mapper/README.md) | `eng::playfield::map_ring_scroll`: mapper neutral del corkscrew (planeaddx/BPLCON1 con fetch ancho, offset del anillo y split); fórmula extraída de `XLimitedPlayfield` con equivalente compile-time/runtime. |
| HOST-068 | [double_buffer_scroll](068_double_buffer_scroll/README.md) | `eng::playfield::DoubleBufferScrollPlayfield`: 2 bitmaps, `flip()` y `hardware_view()` del delantero. |
| HOST-106 | [scroll_saveword_guard](106_scroll_saveword_guard/README.md) | `field/scroll_engine.hpp`: la costura (`save_word`) se **restaura** cuando `add_draw` rechaza el frame (ScopeGuard); en el camino correcto no se restaura. |
| HOST-232 | [draw_target](232_draw_target/README.md) | `playfield::DrawTarget` (Surface+Rasterizer+FramePlan+clip): `fill`/`line`/`frame`/`c2p` y `box()`. |
| HOST-266 | [rect_fill_sink](266_rect_fill_sink/README.md) | Relleno de rect por hardware: `RectFillSink` + `fill_rect_hw` + elección del `BlitterRaster`. |
| HOST-319 | [cpu_primitives](319_cpu_primitives/README.md) | Primitivas CPU optimizadas (`cpu_primitives.hpp`): rect por spans, línea con Bresenham agrupado por fila, polígono even-odd. Base portable (Atari ST). |
| HOST-396 | [scroll_route](396_scroll_route/README.md) | `field/scroll_route.hpp`: ruta de scroll **continua** (H/V/diagonal/circular/Lissajous por velocidad); cada frame mueve <= 1 px por eje (sin saltos) y **al menos un eje** (imagen distinta); Y en `[0, YMax]`. |
| HOST-397 | [xlimited_ring_torus](397_xlimited_ring_torus/README.md) | Toro del anillo de la demo 205 (anillo 320/viewport 208): recorrido Y completo (112 px) con split, offsets distintos por px y cierre sin costura; los tiles del anillo (20) **dividen** la altura del mapa (40). |
| HOST-399 | [scroll_plan](399_scroll_plan/README.md) | `field/scroll_plan.hpp` (`ScrollPlan`) + `apply_scroll_plan`: vocabulario declarativo común (geometría/política/contenido); el corcóscru se siembra sin pisar lo no declarado. §7(e). |
| HOST-412 | [runtime_scroll_geometry](412_runtime_scroll_geometry/README.md) | §7 (geometría runtime): `field/runtime_scroll_geometry.hpp` — la geometría del anillo calculada en runtime equivale al NTTP `StripScrollGeometry` + invariantes. |
| HOST-413 | [scroll_ladder](413_scroll_ladder/README.md) | §7 (geometría runtime): `field/scroll_ladder.hpp` — `ScrollLadder` registra motores con su geometría y `pick` elige el que encaja (sin refactor del motor NTTP). |
