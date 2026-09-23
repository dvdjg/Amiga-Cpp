# HOST-097 — rejilla, isométrico y hex

Respalda `engine/include/eng/core/util/grid.hpp`.

## Qué cubre

- **tile↔píxel**: `grid_to_world`; `world_to_grid<TileW,TileH>` con división *floor*
  correcta con negativos y desplazamiento (tamaño potencia de dos).
- **proyección isométrica** (diamante): `iso_to_screen` y su inversa
  `screen_to_iso<HalfW,HalfH>` (potencia de dos → desplazamientos).
- **hexagonal** (axial): `Hex`, `hex_neighbor`/`hex_neighbors` (6 direcciones) y
  `hex_distance` (vía cúbicas, sin división).

## Coste

Todo entero con `muls.w` y desplazamientos; sin `float` ni divisiones libgcc.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/097_grid
```
