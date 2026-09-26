# HOST-336 — contenido de capa del mundo (`World` acts vs tilemap) — F4b

Respalda `engine/include/eng/scene/world.hpp`: una `Layer` del `World` tiene **contenido**:
actores (por defecto) o **tilemap** (reusando `TileLayer`). Cubre:

- `add_layer` (capa de actores) vs `add_tile_layer` (capa de tilemap);
- `kind()`/`is_tilemap()` y el acceso al `TileLayer` de la capa (id, campos copiados);
- `count`/`find` siguen funcionando.

Es el **modelo** de capas (F4b); la **materialización** de cada capa (playfield/tilemap a
display) es el planner (F4c). Unificar aquí evita que `VirtualScene`/`TileLayer` queden como
modelo aparte del `World`.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/336_world_layers
```
