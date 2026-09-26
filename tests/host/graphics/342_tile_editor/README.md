# HOST-342 — editor de tiles de juego (`tilemap::TileEditor`) — F7.2

Respalda `engine/include/eng/graphics/tilemap/tile_editor.hpp`: vista mutable de alto nivel sobre
una rejilla de `PackedTileCell` (índice + dirty por buffer). Da al juego/consumidor las
operaciones de una capa de tiles sin tocar framebuffers ni Blitter:

- `set_tile(tx,ty,index)` (marca sucio en ambos buffers) / `tile(tx,ty)`;
- `mark_all_dirty()`;
- `dirty_rect(buffer)` (caja envolvente de celdas sucias) y `flush(buffer)`.

Es el `set_tile`/`flush` de `IScrollingLayer`; el **redibujado** (jobs de `FramePlan`/Copper) lo
hace el driver de la capa al consumir el `dirty_rect`. La attribute table NES (paleta por bloques)
queda como tabla paralela (futuro).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/342_tile_editor
```

Ver `ROADMAP_API_COHERENCE.md` §7 (F7.2).
