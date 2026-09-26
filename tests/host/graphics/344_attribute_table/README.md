# HOST-344 — attribute table (`tilemap::AttributeTable`) — F7.2 (`set_attribute`)

Respalda `engine/include/eng/graphics/tilemap/attribute_table.hpp`: rejilla mutable de **paleta por
bloque** (16×16 px, estilo *attribute table* NES), con `set(ax,ay,value)`/`get`/`fill` y límites.
Es la tabla **paralela** al `TileEditor` (cambia la paleta de un bloque, no el tile); el driver de
la capa la traduce a parches de paleta/Copper al redibujar.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/344_attribute_table
```

Ver `docs/engine/NES_CONSUMER.md` y `ROADMAP_API_COHERENCE.md` §7 (F7.2).
