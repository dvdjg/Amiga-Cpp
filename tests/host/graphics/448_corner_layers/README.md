# HOST-448 — corner_layers

Test host del **autotiling por esquinas (dual-grid)** y los **mapas por capas** de
`engine/include/eng/graphics/proc/` (`corner_tiles.hpp` + `layered_map.hpp`): el sistema
que convierte un campo de ocupación (lagos, ríos, islas) en tiles de transición cerrados
por construcción y lo especifica en un tilemap multicapa con colisión independiente del
dibujo.

## Qué cubre

- **Ocupación y máscaras**: `OccupancyMap` (`at` con borde `outside`, `fill`), `corner_mask`
  exacta para las 16 configuraciones alrededor de un bloque 2×2 (0/2/4/8/10/12/15),
  `fill_corner_layout` (malla dual-grid `(tiles+1)²`) y la consistencia de las esquinas
  compartidas entre tiles vecinos.
- **Pintor duro** (`corner_solid`/`paint_corner_hard`): mitades, esquinas, sillas 6/9 por
  **cuadrantes** y tres-esquinas; la **junta casa para todas las parejas de máscaras
  compatibles** (última columna del tile izquierdo == primera del derecho).
- **Pintor orgánico** (`corner_classify`/`generate_organic`): determinismo por semilla,
  máscaras 0/15 planas (el ruido ±44 no cruza el umbral ±96), solo colores A/B/orilla, la
  orilla aparece en la transición, y la **continuidad de la junta con la muestra virtual
  `x=N`** para todas las parejas compatibles (campo bilineal por esquinas compartidas +
  ruido con período que divide a `N`).
- **Materiales** (`paint_corner_materials`): máscara 15 == agua del atlas y máscara 0 ==
  hierba, con la **variante fija 0** (tileable: continúa el patrón en el tile vecino).
- **Stamps**: `stamp_rect`, `stamp_disc` (núcleo garantizado, contorno ruidoso **contiguo
  y sin motas aisladas**) y `stamp_river` (2..3 celdas por fila, banda contigua, entra y
  sale del mapa; versión transpuesta), y `CornerTileset::compose` (paleta del estilo,
  orilla presente, centro del lago en agua, esquina lejana en hierba).
- **Mapa por capas** (`LayeredTileMap`): SoA ground/decal/overhead, `kEmptyTile` = celda
  vacía, transparencia por índice 0, `render_layered_map` con callback de actores **entre**
  decal y overhead (el actor se ve fuera de la copa y la copa lo tapa dentro), `span()`
  que encaja con `eng::playfield::TileLayerMap`, `bake_flags` desde el suelo, consultas
  `flags_at`/`flags_at_pixel` (fuera = `outside`) y `move_and_slide` (revierte el eje
  bloqueado, desliza en el otro, el borde del mundo bloquea).

## Cómo está implementado

Cabeceras sin escalar (ruido entero `tile_noise2`, `mulu.w`/shifts; apto para la carga
del 68000) y sin divisiones por píxel: el período del ruido y `u/v` son `constexpr`. La
continuidad de la junta no es «suerte del ruido»: el campo depende solo de las esquinas
compartidas y el período del ruido **divide** al lado del tile, y el test lo verifica
para las 256 parejas de máscaras.

Evidencia visual opcional (PPM con la paleta del atlas: set de 16 transiciones con
materiales, mundo con río+lago+isla y mapa por capas con actor bajo la copa):

```bash
<binario> --dump out/playground/corner-layers
```

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/448_corner_layers
```

## Estado

Respaldo host de las dos cabeceras (ambas **NO VERIFICADAS** en hardware: sin demo que
las ejercite todavía; ver `docs/engine/architecture/PROCEDURAL_BITMAPS.md`). La demo
119/120 no las usa aún.
