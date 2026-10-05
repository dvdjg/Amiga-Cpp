# Referencia — `graphics/tilemap/`

Modelo **retenido** para tilemaps con scroll, sin heap. El juego aporta memoria externa (arena o recurso UAF-R) para el mapa; el scroll se separa en parte gruesa (tile/word) y fina (píxel), y la lógica de juego no sabe si el Amiga usará `BPLxPT`, `BPLCON1` o blits.

## `PackedTileCell` — `tilemap/tile_scroll.hpp`

Celda de tile con **flags embebidos** (`tile_scroll.hpp`): los bits altos guardan el índice de tile, los bajos los **dirty flags** por buffer (`dirty_buffer_0`/`dirty_buffer_1`, `tile_shift = 2`). Un doble buffer recuerda así qué página necesita actualizar sus tiles ocultos sin forzar un refresco completo. API: `tile_index()`, `dirty_for(buffer_index)`, `set_tile(index)`, `mark_dirty()`, `clear_dirty_for(buffer_index)`. `TileRect` describe un rectángulo de tiles visible/actualizado.

## `AttributeTable` — `tilemap/attribute_table.hpp`

`AttributeTable` es una **rejilla mutable de atributos por celda** (paleta, flags). Permite cambiar la apariencia de zonas del mapa sin re-hornear el tilemap.

## `TileEditor` — `tilemap/tile_editor.hpp`

`TileEditor` (`tile_editor.hpp:26`) es la vista mutable de alto nivel de una capa de tiles: `set_tile(tx, ty, index)` (marca la celda sucia en ambos buffers), `tile(tx, ty)`, `mark_all_dirty()` (al cambiar tileset/paleta), `dirty_rect(buffer)` (caja envolvente de celdas sucias, el rectángulo mínimo a redibujar) y `flush(buffer)` (limpia el dirty de ese buffer) — sin tocar framebuffers ni Blitter.

Volver al [índice de `graphics/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
